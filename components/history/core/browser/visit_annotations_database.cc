// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/history/core/browser/visit_annotations_database.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/ranges/algorithm.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/history/core/browser/url_row.h"
#include "sql/statement.h"
#include "sql/statement_id.h"

namespace history {

namespace {

#define HISTORY_CONTENT_ANNOTATIONS_ROW_FIELDS                        \
  " visit_id,visibility_score,categories,page_topics_model_version,"  \
  "annotation_flags,entities,related_searches,search_normalized_url," \
  "search_terms,alternative_title "
#define HISTORY_CONTEXT_ANNOTATIONS_ROW_FIELDS                    \
  " visit_id,context_annotation_flags,duration_since_last_visit," \
  "page_end_reason,total_foreground_duration "

// Converts the serialized categories into a vector of (`id`, `weight`)
// pairs.
std::vector<VisitContentModelAnnotations::Category>
GetCategoriesFromStringColumn(const std::string& column_value) {
  std::vector<VisitContentModelAnnotations::Category> categories;

  std::vector<std::string> category_strings = base::SplitString(
      column_value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& category_string : category_strings) {
    std::vector<std::string> category_parts = base::SplitString(
        category_string, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

    auto category = VisitContentModelAnnotations::Category::FromStringVector(
        category_parts);
    if (category)
      categories.emplace_back(*category);
  }
  return categories;
}

// Converts categories to something that can be stored in the database.
std::string ConvertCategoriesToStringColumn(
    const std::vector<VisitContentModelAnnotations::Category>& categories) {
  std::vector<std::string> serialized_categories;
  for (const auto& category : categories) {
    serialized_categories.emplace_back(category.ToString());
  }
  return base::JoinString(serialized_categories, ",");
}

// Converts the serialized related searches string into a vector of strings.
std::vector<std::string> GetRelatedSearchesFromStringColumn(
    const std::string& column_value) {
  using std::string_literals::operator""s;
  return base::SplitString(column_value, "\0"s, base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

// Serializes related searches into a string that can be stored in the database.
std::string ConvertRelatedSearchesToStringColumn(
    const std::vector<std::string>& related_searches) {
  // Use the Null character as the separator to serialize the related searches.
  using std::string_literals::operator""s;
  return base::JoinString(related_searches, "\0"s);
}

// An enum of bitmasks to help represent the boolean flags of
// `VisitContextAnnotations` in the database. This avoids having to update
// the schema every time we add/remove/change a bool context annotation. As
// these are persisted to the database, entries should not be renumbered and
// numeric values should never be reused.
enum class ContextAnnotationFlags : uint64_t {
  // True if the user has cut or copied the omnibox URL to the clipboard for
  // this page load.
  kOmniboxUrlCopied = 1 << 0,

  // True if the page was in a tab group when the navigation was committed.
  kIsExistingPartOfTabGroup = 1 << 1,

  // True if the page was NOT part of a tab group when the navigation
  // committed, and IS part of a tab group at the end of the page lifetime.
  kIsPlacedInTabGroup = 1 << 2,

  // True if this page was a bookmark when the navigation was committed.
  kIsExistingBookmark = 1 << 3,

  // True if the page was NOT a bookmark when the navigation was committed and
  // was MADE a bookmark during the page's lifetime. In other words:
  // If `is_existing_bookmark` is true, that implies `is_new_bookmark` is false.
  kIsNewBookmark = 1 << 4,

  // True if the page has been explicitly added (by the user) to the list of
  // custom links displayed in the NTP. Links added to the NTP by History
  // TopSites don't count for this.  Always false on Android, because Android
  // does not have NTP custom links.
  kIsNtpCustomLink = 1 << 5,
};

int64_t ContextAnnotationsToFlags(VisitContextAnnotations context_annotations) {
  int64_t flags = 0;
  if (context_annotations.omnibox_url_copied)
    flags |= static_cast<uint64_t>(ContextAnnotationFlags::kOmniboxUrlCopied);
  if (context_annotations.is_existing_part_of_tab_group) {
    flags |= static_cast<uint64_t>(
        ContextAnnotationFlags::kIsExistingPartOfTabGroup);
  }
  if (context_annotations.is_placed_in_tab_group)
    flags |= static_cast<uint64_t>(ContextAnnotationFlags::kIsPlacedInTabGroup);
  if (context_annotations.is_existing_bookmark)
    flags |= static_cast<uint64_t>(ContextAnnotationFlags::kIsExistingBookmark);
  if (context_annotations.is_new_bookmark)
    flags |= static_cast<uint64_t>(ContextAnnotationFlags::kIsNewBookmark);
  if (context_annotations.is_ntp_custom_link)
    flags |= static_cast<uint64_t>(ContextAnnotationFlags::kIsNtpCustomLink);
  return flags;
}

VisitContextAnnotations ConstructContextAnnotationsWithFlags(
    int64_t flags,
    base::TimeDelta duration_since_last_visit,
    int page_end_reason,
    base::TimeDelta total_foreground_duration) {
  VisitContextAnnotations context_annotations;
  context_annotations.omnibox_url_copied =
      flags & static_cast<uint64_t>(ContextAnnotationFlags::kOmniboxUrlCopied);
  context_annotations.is_existing_part_of_tab_group =
      flags &
      static_cast<uint64_t>(ContextAnnotationFlags::kIsExistingPartOfTabGroup);
  context_annotations.is_placed_in_tab_group =
      flags &
      static_cast<uint64_t>(ContextAnnotationFlags::kIsPlacedInTabGroup);
  context_annotations.is_existing_bookmark =
      flags &
      static_cast<uint64_t>(ContextAnnotationFlags::kIsExistingBookmark);
  context_annotations.is_new_bookmark =
      flags & static_cast<uint64_t>(ContextAnnotationFlags::kIsNewBookmark);
  context_annotations.is_ntp_custom_link =
      flags & static_cast<uint64_t>(ContextAnnotationFlags::kIsNtpCustomLink);
  context_annotations.duration_since_last_visit = duration_since_last_visit;
  context_annotations.page_end_reason = page_end_reason;
  context_annotations.total_foreground_duration = total_foreground_duration;
  return context_annotations;
}
}  // namespace

VisitAnnotationsDatabase::VisitAnnotationsDatabase() = default;
VisitAnnotationsDatabase::~VisitAnnotationsDatabase() = default;

bool VisitAnnotationsDatabase::InitVisitAnnotationsTables() {
  // Content Annotations table.
  if (!GetDB().Execute("CREATE TABLE IF NOT EXISTS content_annotations("
                       "visit_id INTEGER PRIMARY KEY,"
                       "visibility_score NUMERIC,"
                       "floc_protected_score NUMERIC,"
                       "categories VARCHAR,"
                       "page_topics_model_version INTEGER,"
                       "annotation_flags INTEGER NOT NULL,"
                       "entities VARCHAR,"
                       "related_searches VARCHAR,"
                       "search_normalized_url VARCHAR,"
                       "search_terms LONGVARCHAR,"
                       "alternative_title VARCHAR)")) {
    return false;
  }

  // See `AnnotatedVisitRow` and `VisitContextAnnotations` for details about
  // these fields.
  if (!GetDB().Execute("CREATE TABLE IF NOT EXISTS context_annotations("
                       "visit_id INTEGER PRIMARY KEY,"
                       "context_annotation_flags INTEGER NOT NULL,"
                       "duration_since_last_visit INTEGER,"
                       "page_end_reason INTEGER,"
                       "total_foreground_duration INTEGER)")) {
    return false;
  }

  if (!GetDB().Execute("CREATE TABLE IF NOT EXISTS clusters("
                       "cluster_id INTEGER PRIMARY KEY,"
                       "score NUMERIC NOT NULL)")) {
    return false;
  }

  // Represents the many-to-many relationship of `Cluster`s and `Visit`s.
  // `score` here is unique to the visit/cluster combination; i.e. the same
  // visit in another cluster or another visit in the same cluster may have
  // different scores.
  if (!GetDB().Execute("CREATE TABLE IF NOT EXISTS clusters_and_visits("
                       "cluster_id INTEGER NOT NULL,"
                       "visit_id INTEGER NOT NULL,"
                       "score NUMERIC NOT NULL,"
                       "PRIMARY KEY(cluster_id,visit_id))"
                       "WITHOUT ROWID")) {
    return false;
  }

  if (!GetDB().Execute("CREATE INDEX IF NOT EXISTS clusters_for_visit ON "
                       "clusters_and_visits(visit_id)")) {
    return false;
  }

  return true;
}

bool VisitAnnotationsDatabase::DropVisitAnnotationsTables() {
  // Dropping the tables will implicitly delete the indices.
  return GetDB().Execute("DROP TABLE content_annotations") &&
         GetDB().Execute("DROP TABLE context_annotations") &&
         GetDB().Execute("DROP TABLE clusters") &&
         GetDB().Execute("DROP TABLE clusters_and_visits");
}

void VisitAnnotationsDatabase::AddContentAnnotationsForVisit(
    VisitID visit_id,
    const VisitContentAnnotations& visit_content_annotations) {
  DCHECK_GT(visit_id, 0);
  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO content_annotations(" HISTORY_CONTENT_ANNOTATIONS_ROW_FIELDS
      ")VALUES(?,?,?,?,?,?,?,?,?,?)"));
  statement.BindInt64(0, visit_id);
  statement.BindDouble(
      1, static_cast<double>(
             visit_content_annotations.model_annotations.visibility_score));
  statement.BindString(
      2, ConvertCategoriesToStringColumn(
             visit_content_annotations.model_annotations.categories));
  statement.BindInt64(
      3, visit_content_annotations.model_annotations.page_topics_model_version);
  statement.BindInt64(4, visit_content_annotations.annotation_flags);
  statement.BindString(
      5, ConvertCategoriesToStringColumn(
             visit_content_annotations.model_annotations.entities));
  statement.BindString(6, ConvertRelatedSearchesToStringColumn(
                              visit_content_annotations.related_searches));
  statement.BindString(7,
                       visit_content_annotations.search_normalized_url.spec());
  statement.BindString16(8, visit_content_annotations.search_terms);
  statement.BindString(9, visit_content_annotations.alternative_title);

  if (!statement.Run()) {
    DVLOG(0) << "Failed to execute 'content_annotations' insert statement:  "
             << "visit_id = " << visit_id;
  }
}

void VisitAnnotationsDatabase::AddContextAnnotationsForVisit(
    VisitID visit_id,
    const VisitContextAnnotations& visit_context_annotations) {
  DCHECK_GT(visit_id, 0);
  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO context_annotations(" HISTORY_CONTEXT_ANNOTATIONS_ROW_FIELDS
      ")VALUES(?,?,?,?,?)"));
  statement.BindInt64(0, visit_id);
  statement.BindInt64(1, ContextAnnotationsToFlags(visit_context_annotations));
  statement.BindInt64(
      2, visit_context_annotations.duration_since_last_visit.InMicroseconds());
  statement.BindInt(3, visit_context_annotations.page_end_reason);
  statement.BindInt64(
      4, visit_context_annotations.total_foreground_duration.InMicroseconds());

  if (!statement.Run()) {
    DVLOG(0)
        << "Failed to execute visit 'context_annotations' insert statement:  "
        << "visit_id = " << visit_id;
  }
}

void VisitAnnotationsDatabase::UpdateContentAnnotationsForVisit(
    VisitID visit_id,
    const VisitContentAnnotations& visit_content_annotations) {
  DCHECK_GT(visit_id, 0);
  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE content_annotations SET "
      "visibility_score=?,categories=?,"
      "page_topics_model_version=?,"
      "annotation_flags=?,entities=?,"
      "related_searches=?,search_normalized_url=?,search_terms=?,"
      "alternative_title=? "
      "WHERE visit_id=?"));
  statement.BindDouble(
      0, static_cast<double>(
             visit_content_annotations.model_annotations.visibility_score));
  statement.BindString(
      1, ConvertCategoriesToStringColumn(
             visit_content_annotations.model_annotations.categories));
  statement.BindInt64(
      2, visit_content_annotations.model_annotations.page_topics_model_version);
  statement.BindInt64(3, visit_content_annotations.annotation_flags);
  statement.BindString(
      4, ConvertCategoriesToStringColumn(
             visit_content_annotations.model_annotations.entities));
  statement.BindString(5, ConvertRelatedSearchesToStringColumn(
                              visit_content_annotations.related_searches));
  statement.BindString(6,
                       visit_content_annotations.search_normalized_url.spec());
  statement.BindString16(7, visit_content_annotations.search_terms);
  statement.BindString(8, visit_content_annotations.alternative_title);
  statement.BindInt64(9, visit_id);

  if (!statement.Run()) {
    DVLOG(0)
        << "Failed to execute visit 'content_annotations' update statement:  "
        << "visit_id = " << visit_id;
  }
}

bool VisitAnnotationsDatabase::GetContextAnnotationsForVisit(
    VisitID visit_id,
    VisitContextAnnotations* out_context_annotations) {
  DCHECK(out_context_annotations);

  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "SELECT" HISTORY_CONTEXT_ANNOTATIONS_ROW_FIELDS
                     "FROM context_annotations WHERE visit_id=?"));
  statement.BindInt64(0, visit_id);

  if (!statement.Step())
    return false;

  VisitID received_visit_id = statement.ColumnInt64(0);
  DCHECK_EQ(visit_id, received_visit_id);

  // TODO(tommycli): Make sure ConstructContextAnnotationsWithFlags validates
  //  the column values against potential disk corruption, and add tests.
  // The `VisitID` in column 0 is intentionally ignored, as it's not part of
  // `VisitContextAnnotations`.
  *out_context_annotations = ConstructContextAnnotationsWithFlags(
      statement.ColumnInt64(1), base::Microseconds(statement.ColumnInt64(2)),
      statement.ColumnInt(3), base::Microseconds(statement.ColumnInt64(4)));
  return true;
}

bool VisitAnnotationsDatabase::GetContentAnnotationsForVisit(
    VisitID visit_id,
    VisitContentAnnotations* out_content_annotations) {
  DCHECK_GT(visit_id, 0);
  DCHECK(out_content_annotations);

  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "SELECT" HISTORY_CONTENT_ANNOTATIONS_ROW_FIELDS
                     "FROM content_annotations WHERE visit_id=?"));
  statement.BindInt64(0, visit_id);

  if (!statement.Step())
    return false;

  VisitID received_visit_id = statement.ColumnInt64(0);
  DCHECK_EQ(visit_id, received_visit_id);

  out_content_annotations->model_annotations.visibility_score =
      static_cast<float>(statement.ColumnDouble(1));
  out_content_annotations->model_annotations.categories =
      GetCategoriesFromStringColumn(statement.ColumnString(2));
  out_content_annotations->model_annotations.page_topics_model_version =
      statement.ColumnInt64(3);
  out_content_annotations->annotation_flags = statement.ColumnInt64(4);
  out_content_annotations->model_annotations.entities =
      GetCategoriesFromStringColumn(statement.ColumnString(5));
  out_content_annotations->related_searches =
      GetRelatedSearchesFromStringColumn(statement.ColumnString(6));
  out_content_annotations->search_normalized_url =
      GURL(statement.ColumnString(7));
  out_content_annotations->search_terms = statement.ColumnString16(8);
  out_content_annotations->alternative_title = statement.ColumnString(9);
  return true;
}

void VisitAnnotationsDatabase::DeleteAnnotationsForVisit(VisitID visit_id) {
  sql::Statement statement;

  statement.Assign(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM content_annotations WHERE visit_id=?"));
  statement.BindInt64(0, visit_id);
  if (!statement.Run()) {
    DVLOG(0) << "Failed to execute content_annotations delete statement:  "
             << "visit_id = " << visit_id;
  }

  statement.Assign(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM context_annotations WHERE visit_id=?"));
  statement.BindInt64(0, visit_id);
  if (!statement.Run()) {
    DVLOG(0) << "Failed to execute context_annotations delete statement:  "
             << "visit_id = " << visit_id;
  }

  statement.Assign(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM clusters_and_visits WHERE visit_id=?"));
  statement.BindInt64(0, visit_id);
  if (!statement.Run()) {
    DVLOG(0) << "Failed to execute clusters_and_visits delete statement:  "
             << "visit_id = " << visit_id;
  }

  // TODO(manukh): Delete `Cluster`s that are now empty.
}

void VisitAnnotationsDatabase::AddClusters(
    const std::vector<Cluster>& clusters) {
  DCHECK(!clusters.empty());
  // TODO(manukh) Persist scores once we have them.
  sql::Statement clusters_statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "INSERT INTO clusters(score)VALUES(0)"));
  sql::Statement clusters_and_visits_statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO clusters_and_visits(cluster_id,visit_id,score)"
      "VALUES(?,?,0)"));

  for (const auto& cluster : clusters) {
    if (cluster.visits.empty())
      continue;
    clusters_statement.Reset(false);
    if (!clusters_statement.Run()) {
      DVLOG(0) << "Failed to execute 'clusters' insert statement";
      continue;
    }
    const int64_t cluster_id = GetDB().GetLastInsertRowId();
    DCHECK(cluster_id);
    base::ranges::for_each(
        cluster.visits,
        [&](const auto& annotated_visit) {
          clusters_and_visits_statement.Reset(true);
          clusters_and_visits_statement.BindInt64(0, cluster_id);
          clusters_and_visits_statement.BindInt64(
              1, annotated_visit.visit_row.visit_id);
          if (!clusters_and_visits_statement.Run()) {
            DVLOG(0)
                << "Failed to execute 'clusters_and_visits' insert statement:  "
                << "cluster_id = " << cluster_id
                << ", visit_id = " << annotated_visit.visit_row.visit_id;
          }
        },
        &ClusterVisit::annotated_visit);
  }
}

std::vector<int64_t> VisitAnnotationsDatabase::GetRecentClusterIds(
    base::Time minimum_time) {
  // Using `EXISTS` instead of `IN` would result in a full scan of
  // `clusters_and_visits`. Using `JOIN` would produce an equivalent query plan.
  sql::Statement statement(
      GetDB().GetCachedStatement(SQL_FROM_HERE,
                                 "SELECT DISTINCT cluster_id "
                                 "FROM clusters_and_visits "
                                 "WHERE visit_id IN("
                                 "SELECT id FROM visits WHERE visit_time>=?)"
                                 "ORDER BY cluster_id DESC"));
  statement.BindTime(0, minimum_time);

  std::vector<int64_t> cluster_ids;
  while (statement.Step())
    cluster_ids.push_back(statement.ColumnInt64(0));
  return cluster_ids;
}

std::vector<int64_t> VisitAnnotationsDatabase::GetMostRecentClusterIds(
    base::Time inclusive_min_time,
    base::Time exclusive_max_time,
    int max_clusters) {
  DCHECK_GT(max_clusters, 0);
  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT cluster_id "
      "FROM clusters_and_visits "
      "JOIN visits ON visit_id=id "
      "GROUP BY cluster_id "
      "HAVING MAX(visit_time)>=? AND MAX(visit_time)<? "
      "ORDER BY MAX(visit_time) DESC "
      "LIMIT ?"));
  statement.BindTime(0, inclusive_min_time);
  statement.BindTime(1, exclusive_max_time);
  statement.BindInt(2, max_clusters);

  std::vector<int64_t> cluster_ids;
  while (statement.Step())
    cluster_ids.push_back(statement.ColumnInt64(0));
  return cluster_ids;
}

std::vector<VisitID> VisitAnnotationsDatabase::GetVisitIdsInCluster(
    int64_t cluster_id) {
  DCHECK_GT(cluster_id, 0);
  sql::Statement statement(
      GetDB().GetCachedStatement(SQL_FROM_HERE,
                                 "SELECT visit_id "
                                 "FROM clusters_and_visits "
                                 "WHERE cluster_id=? "
                                 "ORDER BY visit_id DESC"));
  statement.BindInt64(0, cluster_id);

  std::vector<VisitID> visit_ids;
  while (statement.Step())
    visit_ids.push_back(statement.ColumnInt64(0));
  return visit_ids;
}

bool VisitAnnotationsDatabase::IsVisitClustered(VisitID visit_id) {
  DCHECK_GT(visit_id, 0);
  sql::Statement statement(
      GetDB().GetCachedStatement(SQL_FROM_HERE,
                                 "SELECT 1 "
                                 "FROM clusters_and_visits "
                                 "WHERE visit_id=? "
                                 "LIMIT 1"));
  statement.BindInt64(0, visit_id);
  return statement.Step();
}

void VisitAnnotationsDatabase::DeleteClusters(
    const std::vector<int64_t>& cluster_ids) {
  if (cluster_ids.empty())
    return;

  sql::Statement clusters_statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM clusters WHERE cluster_id=?"));

  sql::Statement clusters_and_visits_statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM clusters_and_visits WHERE cluster_id=?"));

  for (auto cluster_id : cluster_ids) {
    clusters_statement.Reset(true);
    clusters_statement.BindInt64(0, cluster_id);
    if (!clusters_statement.Run()) {
      DVLOG(0) << "Failed to execute clusters delete statement:  "
               << "cluster_id = " << cluster_id;
    }

    clusters_and_visits_statement.Reset(true);
    clusters_and_visits_statement.BindInt64(0, cluster_id);
    if (!clusters_and_visits_statement.Run()) {
      DVLOG(0) << "Failed to execute clusters_and_visits delete statement:  "
               << "cluster_id = " << cluster_id;
    }
  }
}

bool VisitAnnotationsDatabase::MigrateFlocAllowedToAnnotationsTable() {
  if (!GetDB().DoesTableExist("content_annotations")) {
    NOTREACHED() << " content_annotations table should exist before migration";
    return false;
  }

  // Not all version 43 history has the content_annotations table. So at this
  // point the content_annotations table may already have been initialized with
  // the latest version with a annotation_flags column.
  if (!GetDB().DoesColumnExist("content_annotations", "annotation_flags")) {
    // Add a annotation_flags column to the content_annotations table.
    if (!GetDB().Execute("ALTER TABLE content_annotations ADD COLUMN "
                         "annotation_flags INTEGER DEFAULT 0 NOT NULL")) {
      return false;
    }
  }

  // If there's a matching visit entry in the content_annotations table, migrate
  // the publicly_routable field from the visit entry to the
  // annotation_flags field of the annotation entry.
  if (!GetDB().Execute("UPDATE content_annotations "
                       "SET annotation_flags=1 "
                       "FROM visits "
                       "WHERE visits.id=content_annotations.visit_id AND "
                       "visits.publicly_routable")) {
    return false;
  }

  // Migrate all publicly_routable visit entries that don't have a matching
  // entry in the content_annotations table. The rest of the fields are set to
  // their default value.
  if (!GetDB().Execute("INSERT OR IGNORE INTO content_annotations"
                       "(visit_id,floc_protected_score,categories,"
                       "page_topics_model_version,annotation_flags)"
                       "SELECT id,-1,'',-1,1 FROM visits "
                       "WHERE visits.publicly_routable")) {
    return false;
  }

  return true;
}

bool VisitAnnotationsDatabase::MigrateReplaceClusterVisitsTable() {
  // We don't need to actually copy values from the previous table; it was only
  // rolled out behind a flag.
  return !GetDB().DoesTableExist("cluster_visits") ||
         GetDB().Execute("DROP TABLE cluster_visits");
}

bool VisitAnnotationsDatabase::
    MigrateContentAnnotationsWithoutEntitiesColumn() {
  if (!GetDB().DoesTableExist("content_annotations")) {
    NOTREACHED() << " Content annotations table should exist before migration";
    return false;
  }

  if (GetDB().DoesColumnExist("content_annotations", "entities"))
    return true;

  // Old versions don't have the entities column, we modify the table to add
  // that field.
  return GetDB().Execute(
      "ALTER TABLE content_annotations "
      "ADD COLUMN entities VARCHAR");
}

bool VisitAnnotationsDatabase::
    MigrateContentAnnotationsAddRelatedSearchesColumn() {
  if (!GetDB().DoesTableExist("content_annotations")) {
    NOTREACHED() << " Content annotations table should exist before migration";
    return false;
  }

  if (GetDB().DoesColumnExist("content_annotations", "related_searches"))
    return true;

  // Add the `related_searches` column to the older versions of the table.
  return GetDB().Execute(
      "ALTER TABLE content_annotations "
      "ADD COLUMN related_searches VARCHAR");
}

bool VisitAnnotationsDatabase::MigrateContentAnnotationsAddVisibilityScore() {
  if (!GetDB().DoesTableExist("content_annotations")) {
    NOTREACHED() << " Content annotations table should exist before migration";
    return false;
  }

  if (GetDB().DoesColumnExist("content_annotations", "visibility_score"))
    return true;
  return GetDB().Execute(
      "ALTER TABLE content_annotations "
      "ADD COLUMN visibility_score NUMERIC DEFAULT -1");
}

bool VisitAnnotationsDatabase::
    MigrateContextAnnotationsAddTotalForegroundDuration() {
  if (!GetDB().DoesTableExist("context_annotations")) {
    NOTREACHED() << " Context annotations table should exist before migration";
    return false;
  }

  if (GetDB().DoesColumnExist("context_annotations",
                              "total_foreground_duration"))
    return true;
  // 1000000us = 1s which is the default duration for this DB.
  return GetDB().Execute(
      "ALTER TABLE context_annotations "
      "ADD COLUMN total_foreground_duration NUMERIC DEFAULT -1000000");
}

bool VisitAnnotationsDatabase::MigrateContentAnnotationsAddSearchMetadata() {
  if (!GetDB().DoesTableExist("content_annotations")) {
    NOTREACHED() << " Content annotations table should exist before migration";
    return false;
  }

  if (GetDB().DoesColumnExist("content_annotations", "search_normalized_url") &&
      GetDB().DoesColumnExist("content_annotations", "search_terms")) {
    return true;
  }

  // Add the `search_normalized_url` and `search_terms` columns to the older
  // versions of the table.
  return GetDB().Execute(
      "ALTER TABLE content_annotations "
      "ADD COLUMN search_normalized_url; \n"
      "ALTER TABLE content_annotations ADD COLUMN search_terms LONGVARCHAR");
}

bool VisitAnnotationsDatabase::MigrateContentAnnotationsAddAlternativeTitle() {
  if (!GetDB().DoesTableExist("content_annotations")) {
    NOTREACHED() << "Content annotations table should exist before migration";
    return false;
  }

  if (GetDB().DoesColumnExist("content_annotations", "alternative_title"))
    return true;

  // Add the `alternative_title`column to the older versions of the table.
  return GetDB().Execute(
      "ALTER TABLE content_annotations "
      "ADD COLUMN alternative_title");
}

}  // namespace history
