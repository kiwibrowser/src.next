// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/device_local_account_util.h"

#include <algorithm>

namespace extensions {

namespace {

// Apps/extensions explicitly allowlisted for use in public sessions.
const char* const kPublicSessionAllowlist[] = {
    // Public sessions in general:
    "cbkkbcmdlboombapidmoeolnmdacpkch",  // Chrome RDP
    "inomeogfingihgjfjlpeplalcfajhgai",  // Chrome Remote Desktop
    "djflhoibgkdhkhhcedjiklpkjnoahfmg",  // User Agent Switcher
    "iabmpiboiopbgfabjmgeedhcmjenhbla",  // VNC Viewer
    "haiffjcadagjlijoggckpgfnoeiflnem",  // Citrix Receiver
    "lfnfbcjdepjffcaiagkdmlmiipelnfbb",  // Citrix Receiver (branded)
    "mfaihdlpglflfgpfjcifdjdjcckigekc",  // ARC Runtime
    "ngjnkanfphagcaokhjecbgkboelgfcnf",  // Print button
    "cjanmonomjogheabiocdamfpknlpdehm",  // HP printer driver
    "ioofdkhojeeimmagbjbknkejkgbphdfl",  // RICOH Print for Chrome
    "pmnllmkmjilbojkpgplbdmckghmaocjh",  // Scan app by François Beaufort
    "haeblkpifdemlfnkogkipmghfcbonief",  // Charismathics Smart Card Middleware
    "mpnkhdpphjiihmlmkcamhpogecnnfffa",  // Service NSW Kiosk Utility
    "npilppbicblkkgjfnbmibmhhgjhobpll",  // QwickACCESS
    // TODO(isandrk): Only on the allowlist for the purpose of getting the soft
    // MGS warning.  Remove
    // once dynamic MGS warnings are implemented.
    "ppkfnjlimknmjoaemnpidmdlfchhehel",  // VMware Horizon Client for Chrome

    // Libraries:
    "aclofikceldphonlfmghmimkodjdmhck",  // Ancoris login component
    "eilbnahdgoddoedakcmfkcgfoegeloil",  // Ancoris proxy component
    "ceehlgckkmkaoggdnjhibffkphfnphmg",  // Libdata login
    "fnhgfoccpcjdnjcobejogdnlnidceemb",  // OverDrive

    // Education:
    "cmeclblmdmffdgpdlifgepjddoplmmal",  //  Imagine Learning

    // Retail mode:
    "bjfeaefhaooblkndnoabbkkkenknkemb",  // 500 px demo
    "ehcabepphndocfmgbdkbjibfodelmpbb",  // Angry Birds demo
    "kgimkbnclbekdkabkpjhpakhhalfanda",  // Bejeweled demo
    "joodangkbfjnajiiifokapkpmhfnpleo",  // Calculator
    "fpgfohogebplgnamlafljlcidjedbdeb",  // Calendar demo
    "jkoildpomkimndcphjpffmephmcmkfhn",  // Chromebook Demo App
    "lbhdhapagjhalobandnbdnmblnmocojh",  // Crackle demo
    "ielkookhdphmgbipcfmafkaiagademfp",  // Custom bookmarks
    "kogjlbfgggambihdjcpijgcbmenblimd",  // Custom bookmarks
    "ogbkmlkceflgpilgbmbcfbifckpkfacf",  // Custom bookmarks
    "pbbbjjecobhljkkcenlakfnkmkfkfamd",  // Custom bookmarks
    "jkbfjmnjcdmhlfpephomoiipbhcoiffb",  // Custom bookmarks
    "dgmblbpgafgcgpkoiilhjifindhinmai",  // Custom bookmarks
    "iggnealjakkgfofealilhkkclnbnfnmo",  // Custom bookmarks
    "lplkobnahgbopmpkdapaihnnojkphahc",  // Custom bookmarks
    "lejnflfhjpcannpaghnahbedlabpmhoh",  // Custom bookmarks
    "dhjmfhojkfjmfbnbnpichdmcdghdpccg",  // Cut the Rope demo
    "ebkhfdfghngbimnpgelagnfacdafhaba",  // Deezer demo
    "npnjdccdffhdndcbeappiamcehbhjibf",  // Docs.app demo
    "ekgadegabdkcbkodfbgidncffijbghhl",  // Duolingo demo
    "iddohohhpmajlkbejjjcfednjnhlnenk",  // Evernote demo
    "bjdhhokmhgelphffoafoejjmlfblpdha",  // Gmail demo
    "nldmakcnfaflagmohifhcihkfgcbmhph",  // Gmail offline demo
    "mdhnphfgagkpdhndljccoackjjhghlif",  // Google Drive demo
    "dondgdlndnpianbklfnehgdhkickdjck",  // Google Keep demo
    "amfoiggnkefambnaaphodjdmdooiinna",  // Google Play Movie and TV demo
    "fgjnkhlabjcaajddbaenilcmpcidahll",  // Google+ demo
    "ifpkhncdnjfipfjlhfidljjffdgklanh",  // Google+ Photos demo
    "cgmlfbhkckbedohgdepgbkflommbfkep",  // Hangouts.app demo
    "ndlgnmfmgpdecjgehbcejboifbbmlkhp",  // Hash demo
    "edhhaiphkklkcfcbnlbpbiepchnkgkpn",  // Helper.extension demo
    "jckncghadoodfbbbmbpldacojkooophh",  // Journal demo
    "diehajhcjifpahdplfdkhiboknagmfii",  // Kindle demo
    "idneggepppginmaklfbaniklagjghpio",  // Kingsroad demo
    "nhpmmldpbfjofkipjaieeomhnmcgihfm",  // Menu.app demo
    "kcjbmmhccecjokfmckhddpmghepcnidb",  // Mint demo
    "onbhgdmifjebcabplolilidlpgeknifi",  // Music.app demo
    "kkkbcoabfhgekpnddfkaphobhinociem",  // Netflix demo
    "adlphlfdhhjenpgimjochcpelbijkich",  // New York Times demo
    "cgefhjmlaifaamhhoojmpcnihlbddeki",  // Pandora demo
    "kpjjigggmcjinapdeipapdcnmnjealll",  // Pixlr demo
    "ifnadhpngkodeccijnalokiabanejfgm",  // Pixsta demo
    "klcojgagjmpgmffcildkgbfmfffncpcd",  // Plex demo
    "nnikmgjhdlphciaonjmoppfckbpoinnb",  // Pocket demo
    "khldngaiohpnnoikfmnmfnebecgeobep",  // Polarr Photo demo
    "aleodiobpjillgfjdkblghiiaegggmcm",  // Quickoffice demo
    "nifkmgcdokhkjghdlgflonppnefddien",  // Sheets demo
    "hdmobeajeoanbanmdlabnbnlopepchip",  // Slides demo
    "ikmidginfdcbojdbmejkeakncgdbmonc",  // Soundtrap demo
    "dgohlccohkojjgkkfholmobjjoledflp",  // Spotify demo
    "dhmdaeekeihmajjnmichlhiffffdbpde",  // Store.app demo
    "onklhlmbpfnmgmelakhgehkfdmkpmekd",  // Todoist demo
    "jeabmjjifhfcejonjjhccaeigpnnjaak",  // TweetDeck demo
    "gnckahkflocidcgjbeheneogeflpjien",  // Vine demo
    "pdckcbpciaaicoomipamcabpdadhofgh",  // Weatherbug demo
    "biliocemfcghhioihldfdmkkhnofcgmb",  // Webcam Toy demo
    "bhfoghflalnnjfcfkaelngenjgjjhapk",  // Wevideo demo
    "pjckdjlmdcofkkkocnmhcbehkiapalho",  // Wunderlist demo
    "pbdihpaifchmclcmkfdgffnnpfbobefh",  // YouTube demo

    // New demo mode:
    "lpmakjfjcconjeehbidjclhdlpjmfjjj",  // Highlights app
    "iggildboghmjpbjcpmobahnkmoefkike",  // Highlights app (eve)
    "elhbopodaklenjkeihkdhhfaghalllba",  // Highlights app (nocturne)
    "gjeelkjnolfmhphfhhjokaijbicopfln",  // Highlights app (other)
    "mnoijifedipmbjaoekhadjcijipaijjc",  // Screensaver
    "gdobaoeekhiklaljmhladjfdfkigampc",  // Screensaver (eve)
    "lminefdanffajachfahfpmphfkhahcnj",  // Screensaver (nocturne)
    "fafhbhdboeiciklpkminlncemohljlkj",  // Screensaver (kukui)
    "bnabjkecnachpogjlfilfcnlpcmacglh",  // Screensaver (other)

    // Testing extensions:
    "ongnjlefhnoajpbodoldndkbkdgfomlp",  // Show Managed Storage
    "ilnpadgckeacioehlommkaafedibdeob",  // Enterprise DeviceAttributes
    "oflckobdemeldmjddmlbaiaookhhcngo",  // Citrix Receiver QA version
    "behllobkkfkfnphdnhnkndlbkcpglgmj",  // Autotest

    // Google Apps:
    "mclkkofklkfljcocdinagocijmpgbhab",  // Google input tools
    "gbkeegbaiigmenfmjfclcdgdpimamgkj",  // Office Editing Docs/Sheets/Slides
    "aapbdbdomjkkjkaonfhkkikfgjllcleb",  // Google Translate
    "mgijmajocgfcbeboacabfgobmjgjcoja",  // Google Dictionary
    "mfhehppjhmmnlfbbopchdfldgimhfhfk",  // Google Classroom
    "mkaakpdehdafacodkgkpghoibnmamcme",  // Google Drawings
    "pnhechapfaindjhompbnflcldabbghjo",  // Secure Shell
    "fcgckldmmjdbpdejkclmfnnnehhocbfp",  // Google Finance
    "jhknlonaankphkkbnmjdlpehkinifeeg",  // Google Forms
    "jndclpdbaamdhonoechobihbbiimdgai",  // Chromebook Recovery Utility
    "aohghmighlieiainnegkcijnfilokake",  // Google Docs
    "eemlkeanncmjljgehlbplemhmdmalhdc",  // Chrome Connectivity Diagnostics
    "eoieeedlomnegifmaghhjnghhmcldobl",  // Google Apps Script
    "ndjpildffkeodjdaeebdhnncfhopkajk",  // Network File Share for Chrome OS
    "pfoeakahkgllhkommkfeehmkfcloagkl",  // Fusion Tables
    "aapocclcgogkmnckokdopfmhonfmgoek",  // Google Slides
    "khpfeaanjngmcnplbdlpegiifgpfgdco",  // Smart Card Connector
    "hmjkmjkepdijhoojdojkdfohbdgmmhki",  // Google Keep - notes and lists
    "felcaaldnbdncclmgdcncolpebgiejap",  // Google Sheets
    "khkjfddibboofomnlkndfedpoccieiee",  // Study Kit
    "becloognjehhioodmnimnehjcibkloed",  // Coding with Chrome
    "hfhhnacclhffhdffklopdkcgdhifgngh",  // Camera
    "adokjfanaflbkibffcbhihgihpgijcei",  // Share to Classroom
    "heildphpnddilhkemkielfhnkaagiabh",  // Legacy Browser Support
    "lpcaedmchfhocbbapmcbpinfpgnhiddi",  // Google Keep Chrome Extension
    "ldipcbpaocekfooobnbcddclnhejkcpn",  // Google Scholar Button
    "nnckehldicaciogcbchegobnafnjkcne",  // Google Tone
    "pfmgfdlgomnbgkofeojodiodmgpgmkac",  // Data Saver
    "djcfdncoelnlbldjfhinnjlhdjlikmph",  // High Contrast
    "ipkjmjaledkapilfdigkgfmpekpfnkih",  // Color Enhancer
    "kcnhkahnjcbndmmehfkdnkjomaanaooo",  // Google Voice
    "nlbjncdgjeocebhnmkbbbdekmmmcbfjd",  // RSS Subscription Extension
    "aoggjnmghgmcllfenalipjhmooomfdce",  // SAML SSO for Chrome Apps
    "fhndealchbngfhdoncgcokameljahhog",  // Certificate Enrollment for Chrome OS
    "npeicpdbkakmehahjeeohfdhnlpdklia",  // WebRTC Network Limiter
    "hdkoikmfpncabbdniojdddokkomafcci",  // SSRS Reporting Fix for Chrome
};

}  // namespace

bool IsAllowlistedForPublicSession(const std::string& extension_id) {
  return std::find(std::begin(kPublicSessionAllowlist),
                   std::end(kPublicSessionAllowlist),
                   extension_id) != std::end(kPublicSessionAllowlist);
}

}  // namespace extensions
