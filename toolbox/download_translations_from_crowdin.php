<?php
// php download_translations_from_crowdin.php <API_KEY>
function rglob($pattern, $flags = 0) {
    $files = glob($pattern, $flags);
    foreach (glob(dirname($pattern).'/*', GLOB_ONLYDIR|GLOB_NOSORT) as $dir) {
        $files = array_merge($files, rglob($dir.'/'.basename($pattern), $flags));
    }
    return $files;
}

$url = 'https://api.crowdin.com/api/project/kiwibrowser/export?key=' . $argv[1];
passthru('curl -s ' . escapeshellarg($url));
$zip_url = 'https://api.crowdin.com/api/project/kiwibrowser/download/all.zip?key=' . $argv[1];
passthru('rm ../translations.zip ; wget -q ' . escapeshellarg($zip_url) . ' -O ../translations.zip');
passthru('rm -rf ../translations_tmp/*');
passthru('unzip -q -o -d ../translations_tmp/ ../translations.zip');
passthru('chmod -R 744 ../translations_tmp/');

// This languages cannot be embedded in Chromium
passthru('rm -rf ../translations_tmp/af');
passthru('rm -rf ../translations_tmp/he');
passthru('rm -rf ../translations_tmp/et');
passthru('rm -rf ../translations_tmp/no');
$folders = glob('../translations_tmp/*');
foreach ($folders as $folder) {
  if (stripos($folder, 'values-') === false) {
    $nfolder = str_replace('-', '-r', $folder);
    rename($folder, str_replace('../translations_tmp/', '../translations_tmp/values-', $nfolder));
  }
}
$files = glob('../translations_tmp/*/*');
foreach ($files as $file)
{
  echo $file . "\n";
  // we remove unused / outdated translation strings
  passthru("sed -i '/facebook_app_id/d' " . $file);
  passthru("sed -i '/fb_login_protocol_scheme/d' " . $file);
  passthru("sed -i '/homepage_preferences_enable_news_title/d' " . $file);
  passthru("sed -i '/homepage_preferences_enable_news_summary/d' " . $file);
}
passthru('mkdir -p chrome/android/java/res/');
passthru('cp -rf ../translations_tmp/* chrome/android/java/res/');
passthru('mkdir -p chrome/android/java/res/values-es-rUS');
passthru('cp -rf chrome/android/java/res/values-es-rES/* chrome/android/java/res/values-es-rUS/');
