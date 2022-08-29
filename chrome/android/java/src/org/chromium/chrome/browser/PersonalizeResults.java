package org.chromium.chrome.browser;

import org.chromium.base.Log;
import org.chromium.base.ContextUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.preferences.Pref;

import java.net.URL;
import java.net.MalformedURLException;

public class PersonalizeResults {
    public static void Execute(Tab tab) {
       final boolean shouldRewrapText = ContextUtils.getAppSharedPreferences().getBoolean("text_rewrap", false);
       final boolean shouldRemoveAmp = ContextUtils.getAppSharedPreferences().getBoolean("avoid_amp_websites", true);
       if (shouldRemoveAmp && tab != null && IsSearchUrl(tab.getUrl().getSpec())) {
          tab.getWebContents().evaluateJavaScript(AMP_SCRIPT, null);
       }
       if (tab != null && shouldRewrapText) {
          tab.getWebContents().evaluateJavaScript("(function() { var pendingUpdate=false;function viewportHandler(event){if(pendingUpdate)return;pendingUpdate=true;requestAnimationFrame(()=>{pendingUpdate=false;document.getElementsByTagName('html')[0].style.maxWidth=window.visualViewport.width+'px';var miniLeft=visualViewport.offsetLeft;var miniTop = -(visualViewport.offsetTop + visualViewport.offsetTop * ((window.pageYOffset / window.innerHeight) / 2));document.getElementsByTagName('html')[0].style.transition='0s ease-in-out';if (miniLeft == 0 && miniTop == 0) { document.getElementsByTagName('html')[0].style.transform=''; } else { document.getElementsByTagName('html')[0].style.transform='translate('+miniLeft+'px, '+miniTop+'px) scale(1.0)'; } })}window.visualViewport.addEventListener('resize',viewportHandler);window.visualViewport.addEventListener('scroll', viewportHandler); })();", null);
       }
       if (tab != null && tab.getUrl().getSpec().startsWith("https://chrome.google.com/webstore")) {
          tab.getWebContents().evaluateJavaScript("(function() { if (!document.location.href.includes('https://chrome.google.com/webstore')) { return; } " + MAKE_USER_AGENT_WRITABLE + " window.navigator.userAgent='Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/99.0.4844.84 Safari/537.36'; window.addEventListener('load', function() { if (document.location.pathname == '/webstore/unsupported') { document.location = '/webstore/'; } var node = document.createElement('style');    document.body.appendChild(node);    window.addStyleString = function(str) {        node.innerHTML = str;    }; addStyleString('div { visibility: visible !important; } '); }); })();", null);
       }
       if (tab != null && tab.getUrl().getSpec().startsWith("https://chrome.google.com/webstore") && !ContextUtils.getAppSharedPreferences().getBoolean("cws_mobile_friendly", false)) {
          tab.getWebContents().evaluateJavaScript("(function() { if (!document.location.href.includes('https://chrome.google.com/webstore')) { return; } window.addEventListener('load', function() { var t=document.querySelector('meta[name=\"viewport\"]');t&&(t.content=\"initial-scale=0.1\",t.content=\"width=1200\") }); })();", null);
       }
       if (tab != null && tab.getUrl().getSpec().startsWith("https://chrome.google.com/webstore") && ContextUtils.getAppSharedPreferences().getBoolean("cws_mobile_friendly", false)) {
          tab.getWebContents().evaluateJavaScript(CWS_MOBILE_SCRIPT, null);
       }
       if (tab != null && tab.getUrl().getSpec().startsWith("https://microsoftedge.microsoft.com/addons")) {
          tab.getWebContents().evaluateJavaScript(EDGE_SCRIPT, null);
       }
       if (tab != null && (tab.getUrl().getSpec().startsWith("https://m.facebook.com/messenger/install")
                       || tab.getUrl().getSpec().startsWith("https://m.facebook.com/messages"))) {
          tab.getWebContents().evaluateJavaScript(MESSENGER_SCRIPT, null);
       }
       if (tab != null && tab.getUrl().getSpec().contains("messenger.com/")) {
          tab.getWebContents().evaluateJavaScript(MESSENGER_VIEWPORT_SCRIPT, null);
       }
       if (tab != null && tab.getUrl().getSpec().startsWith("https://m.facebook.com/")) {
          tab.getWebContents().evaluateJavaScript("(function(){ if (!document.location.href.includes('https://m.facebook.com/')) { return; } document.querySelector('body.touch').style = \"cursor:default\";})();", null);
       }
       if (tab != null && tab.getUrl().getSpec().startsWith("https://translate.google.com/translate_c")) {
          tab.getWebContents().evaluateJavaScript("(function(){ if (!document.location.href.includes('https://translate.google.com/translate_c')) { return; } var b=document.getElementById(\"gt-nvframe\");if(b){b.style.position='unset';document.body.style.top='0px'}else{var child=document.createElement('iframe');child.id='gt-nvframe';child.src=document.location.href.replace('/translate_c','/translate_nv');child.style.width='100%';child.style.height='93px';document.body.insertBefore(child,document.body.firstChild);var t=document.querySelector('meta[name=\"viewport\"]');if(!t){var metaTag=document.createElement('meta');metaTag.name='viewport';metaTag.content='width=device-width, initial-scale=1.0';document.body.appendChild(metaTag)}}})();", null);
       }
       if (tab != null && (tab.getUrl().getSpec().startsWith("chrome://")
                       || tab.getUrl().getSpec().startsWith("chrome-extension://")
                       || tab.getUrl().getSpec().startsWith("kiwi://"))) {
          tab.getWebContents().evaluateJavaScript("(function() { if (!document.location.href.includes('chrome://') && !document.location.href.includes('chrome-extension://') && !document.location.href.includes('kiwi://')) { return; } " + ADAPT_TO_MOBILE_VIEWPORT + "})();", null);
       }
       if (tab != null && ContextUtils.getAppSharedPreferences().getBoolean("accept_cookie_consent", true) && (tab.getUrl().getSpec().startsWith("http://") || tab.getUrl().getSpec().startsWith("https://"))) {
          tab.getWebContents().evaluateJavaScript("(function(){function clickItem(elem) { elem.click(); } function acceptViaAPIs(){typeof window.__cmpui=='function'&&window.__cmpui('setAndSaveAllConsent',!0);typeof window.Didomi=='object'&&window.Didomi.setUserAgreeToAll()}window.globalObserver=null;function setupObserver(){if(!window.globalObserver){var newelem=document.createElement('style');newelem.innerHTML='.qc-cmp-showing { visibility: hidden !important; } body.didomi-popup-open { overflow: auto !important; } #didomi-host { visibility: hidden !important; }';document.body.appendChild(newelem);var MutationObserver=window.MutationObserver||window.WebKitMutationObserver;window.globalObserver=new MutationObserver(check);window.globalObserver.observe(window.document.documentElement,{childList:true,subtree:true});window.setTimeout(function(){window.globalObserver.disconnect();window.globalObserver=null},15000)}check()}function check(){window.setTimeout(function(){var listeners=[];listeners.push({selector:'#qcCmpUi',fn:acceptViaAPIs});listeners.push({selector:'#didomi-popup',fn:acceptViaAPIs});listeners.push({selector: '.accept-cookies-button,#purch-gdpr-banner__accept-button,#bbccookies-continue-button,.user-action--accept,.consent-accept,.bcpConsentOKButton,.button.accept,#footer_tc_privacy_button,button[aria-label=\"Button to collapse the message\"],.gdpr-form>.btn[value=\"Continue\"],button[on^=\"tap:\"][on$=\".accept\"],button[on^=\"tap:\"][on$=\".dismiss\"],.js-cookies-button,.app-offer__close_js,.lg-cc__button_type_action',fn: clickItem});for(var i=0,len=listeners.length,listener,elements;i<len;i++){listener=listeners[i];elements=window.document.querySelectorAll(listener.selector);for(var j=0,jLen=elements.length,element;j<jLen;j++){element=elements[j];if(!element.ready){element.ready=true;listener.fn.call(element, element)}}}},5)}window.addEventListener('DOMContentLoaded',setupObserver);check()})();", null);
       }
    }

    private static boolean IsSearchUrl(String sUrl) {
        if (sUrl == null || sUrl.isEmpty()) {
          return false;
        }

        try {
          URL url = new URL(sUrl);
            String sHost = url.getHost();
            if (sHost.contains(".google."))
                return true;
        } catch(MalformedURLException e) {
          Log.w("Kiwi", "MalformedURLException "+ e.getMessage());
        }

        return false;
    }

    private static final String MAKE_USER_AGENT_WRITABLE = ""
+"(function() {"
+"    function createProperty(value) {"
+"        var _value = value;"
+""
+"        function _get() {"
+"            return _value"
+"        }"
+""
+"        function _set(v) {"
+"            _value = v"
+"        }"
+"        return {"
+"            'get': _get,"
+"            'set': _set"
+"        }"
+"    };"
+""
+"    function makePropertyWritable(objBase, objScopeName, propName, initValue) {"
+"        var newProp, initObj;"
+"        if (objBase && objScopeName in objBase && propName in objBase[objScopeName]) {"
+"            if (typeof initValue === 'undefined') {"
+"                initValue = objBase[objScopeName][propName]"
+"            }"
+"            newProp = createProperty(initValue);"
+"            try {"
+"                Object.defineProperty(objBase[objScopeName], propName, newProp)"
+"            } catch (e) {"
+"                initObj = {};"
+"                initObj[propName] = newProp;"
+"                try {"
+"                    objBase[objScopeName] = Object.create(objBase[objScopeName], initObj)"
+"                } catch (e) {}"
+"            }"
+"        }"
+"    };"
+"    makePropertyWritable(window, 'navigator', 'userAgent');"
+"})();";

    private static final String ADAPT_TO_MOBILE_VIEWPORT = ""
+"(function() {"
+"window.addEventListener('load', function() {"
+"    var t = document.querySelector('meta[name=\"viewport\"]');"
+"    t && (t.content = 'initial-scale=1', t.content = 'width=device-width');"
+"    if (!t) {"
+"        var metaTag = document.createElement('meta');"
+"        metaTag.name = 'viewport';"
+"        metaTag.content = 'width=device-width, initial-scale=1.0';"
+"        document.body.appendChild(metaTag);"
+"    }"
+"});"
+"})();";

    private static final String MESSENGER_SCRIPT = ""
+"(function() {"
+"window.addEventListener('load', function() {"
+"if (!document.location.href.includes('https://m.facebook.com/messenger/install') && !document.location.href.includes('https://m.facebook.com/messages')) { return; } "
+"var gotomessenger = document.createElement('div');"
+"gotomessenger.innerHTML = \"<a href='https://www.messenger.com' target='_blank' style='margin: 2rem; display: inline-block;'><b>Go to www.messenger.com instead</a>\";"
+"gotomessenger.id = '_kb_gotomessenger';"
+"var e1 = document.querySelector('._8rws') || document.querySelector('._2bu8');"
+"if (!document.getElementById('_kb_gotomessenger'))"
+"e1.parentNode.insertBefore(gotomessenger, e1.nextSibling);"
+"});"
+"})();";

    private static final String MESSENGER_VIEWPORT_SCRIPT = ""
+"(function() {"
+"if (!document.location.href.includes('messenger.com/')) { return; } "
+"var sheet = document.createElement('style');"
+"sheet.innerHTML = '.jgljxmt5 { min-height: calc(93vh - var(--header-height)); } .g0mhvs5p.g0mhvs5p { width: 150px; }';"
+"document.body.appendChild(sheet);"
+"})();";

    private static final String AMP_SCRIPT = ""
+"(function() {"
+"function _cleanupAmp()"
+"{"
+"  document.querySelectorAll('a[data-amp-cur]').forEach(function(a) {"
+"    a.href = a.getAttribute('data-amp-cur');"
+"    console.log('Detected AMP item: (link: ' + a.href + ')');"
+"    if (!a.href || a.href.includes('/search')) { a.href = a.getAttribute('data-amp'); console.log('Corrected AMP item: (link: ' + a.href + ')'); }"
+"    if (a.href.indexOf('?') == -1) { a.href = a.href + '?'; }"
+"    a.removeAttribute('data-amp');"
+"    a.removeAttribute('data-amp-cur');"
+"    a.removeAttribute('ping');"
+"  });"
+""
+"  document.querySelectorAll('span[aria-label=\"AMP logo\"]').forEach(function(a) {"
+"     a.style.display='none';"
+"  });"
+"  if (document.getElementsByClassName('amp-cantxt').length >= 1 && document.location.href.match(/\\/amp[\\/|\\.]/)) { document.location.replace(document.getElementsByClassName('amp-cantxt')[0].innerText); }"
+"  if (document.location.href.match(/\\/amp[\\/|\\.]/) && document.querySelector('head > link[rel=\"canonical\"]') != null && document.querySelector('head > link[rel=\"canonical\"]').href != document.location.href) { document.location.replace(document.querySelector('head > link[rel=\"canonical\"]').href); };"
+"}"
+""
+"document.addEventListener('DOMNodeInserted', _cleanupAmp);"
+"_cleanupAmp();"
+"})();";

    private static final String EDGE_SCRIPT = ""
+"(function() {"
+"    if (!document.location.href.includes('https://microsoftedge.microsoft.com/addons')) {"
+"        return;"
+"    }"
+ MAKE_USER_AGENT_WRITABLE
+"    window.navigator.userAgent=window.navigator.userAgent + ' Edg/' + window.navigator.appVersion.match(/Chrome\\/(\\d+(:?\\.\\d+)+)/)[1];"
+"    var _kb_setIntervalCnt = 0;"
+"    var _kb_setInterval = window.setInterval(function() {"
+"        var xpath = function(xpathToExecute) {"
+"            var result = [];"
+"            var nodesSnapshot = document.evaluate(xpathToExecute, document, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);"
+"            for (var i = 0; i < nodesSnapshot.snapshotLength; i++) {"
+"                result.push(nodesSnapshot.snapshotItem(i));"
+"            }"
+"            return result;"
+"        };"
+"        xpath(\"//button[contains(@id,'getOrRemoveButton')]\").forEach(function(individualButton) {"
+"            individualButton.setAttribute('style', 'opacity: 1; background: rgb(0, 120, 212) !important; height: 60px; cursor: pointer !important;');"
+"            individualButton.removeAttribute('disabled');"
+"            individualButton.innerHTML = \"<a href=https://edge.microsoft.com/extensionwebstorebase/v1/crx?response=redirect&acceptformat=crx3&x=id%3D\" + individualButton.id.split('-')[1] + \"%26installsource%3Dondemand%26uc target='_blank' style='color: white; text-decoration: none'><b>Get CRX</b><br>(Hold and tap<br>Download Link)</a>\";"
+"        });"
+"        if (_kb_setIntervalCnt++ >= 10) { window.clearInterval(_kb_setInterval); }"
+"    }, 1000);"
+"})();";

    private static final String CWS_MOBILE_SCRIPT = ""
+"(function() {"
+"if (!document.location.href.includes('https://chrome.google.com/webstore')) {return;}"
+"function waitForElementToDisplay(selector,callback,checkFrequencyInMs,timeoutInMs){var startTimeInMs=Date.now();!function loopSearch(){null==document.querySelector(selector)?setTimeout((function(){timeoutInMs&&Date.now()-startTimeInMs>timeoutInMs||loopSearch()}),checkFrequencyInMs):callback()}()}"
+"window.addEventListener(\"load\", function () { console.log(\"Inject cws_mobile_script \"); "
+"if(document.getElementById('lrdnjct') == undefined){"
+"document.querySelector(\"meta[name=viewport]\").setAttribute(\"content\", \"width=device-width, initial-scale=1.0\");"
+"var nodeStyle = document.createElement('style');"
+"nodeStyle.innerHTML = '.showNav{margin:0!important}.a-na-d-cb{border:none}.a-na-d-K-A{margin:0}.g-na-ib-k{padding:0}span.FokDXb.g-Qc-s{margin:0;padding:.4rem}input#searchbox-input{padding:0 .4rem}.a-na-d-Oa{white-space:unset}.a-na-d-K-A-w{flex-direction:column;margin:0}.F-x{width:100%!important}.F-k,.S-Rc-qa .a-na-d,.a-P-d{width:auto!important}.a-K-o{padding-top:8px!important}.h-a-x{text-align:center}.HWJfBb{height:90vh!important;overflow-y:scroll!important;box-shadow:0 3px 6px rgba(0,0,0,.16),0 3px 6px rgba(0,0,0,.23)!important}.a-na-d-B1neQd-cb,div .a-P-d-k{margin:0;border-radius:5px;padding:5px;width:100%!important;box-sizing:border-box;box-shadow:0 3px 6px rgb(0 0 0 / 16%),0 3px 6px rgb(0 0 0 / 23%)}.e-f-w-Va{width:100%!important}.O-j{padding:.5rem}.O-j-gb{margin:0}.yrk3fc{margin:10px 16px!important}span .h-n-j-Z-ea-aa{margin-top:10px!important}.O-j-Ic-c,.e-f-yb-w-ie,.gpx3nd.Ka-Ia-j,.h-ja-sb{display:none}.PNF6le{text-transform:none!important}img.a-P-d-A,img.a-na-d-A{height:auto!important;width:100%!important;border-radius:.5rem}.h-C-b-i-k{transform:scale(.5)}.e-f-pa,.h-C-b-i-k .i-F-Rb-k,section.e-f-b-x{margin:0}.C-b.e-f-b-x{margin:auto}.e-f-b-j.g-b-j.g-b-j-O{flex-direction:column;height:auto;margin-top:1rem}.F-n-J{position:fixed!important;left:0!important;margin-left:-450px;height:100vh!important;width:100vw!important;max-width:450px;z-index:1149!important;transition:margin .4s ease}.e-f-n-Va{gap:1rem;align-items:center;justify-content:center}.e-f-w{text-overflow:unset;white-space:break-spaces}.h-z-Ba-ca,.z-J-cGXGTb-Fg-c{margin:0}.F-ia-k.S-ph.S-Rc-qa,.h-ba-Eb.ba-Eb.pd-Ye-Qa{padding:0}article.tNOBCb-b-n-x,article.tNOBCb-b-p-x.tNOBCb-b-p-x-cb{margin:0;padding:0;border:0}.Hc-o,.e-f-o,.tNOBCb-b,.z-J-w-k,span.z-Ha-j{flex-direction:column;gap:.5rem}.C-b-p-j.C-b-p-j-Wd.Ka-Ia-j{flex-direction:column;margin:0;padding:.5rem}.C-b-p-j-kk-dk.Ka-Ia-j{margin:0;margin-top:1rem;padding:.5rem;border:0;border-top:1px solid #f1f3f4}div[role=row]{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.5rem;padding:.5rem}span.e-f-yb-w{flex-direction:column;gap:.5rem;width:calc(100% + 60px);border-radius:.5rem;padding:.5rem;margin-left:calc(-60px - 1rem);background:#f0f0f0}img.e-f-s{margin-right:0;align-self:baseline}.C-b-i.i-Xa-U-S.i-Xa-Fa .i-Xa-U-S-ti,.i-Xa-U-S-ti{width:25px;height:25px}.f-rd>.e-f{min-width:unset;padding:1rem;box-sizing:border-box}#mnbtn3{float:left;padding:4px;display:inline-block;cursor:pointer;z-index:200}.bar1,.bar2,.bar3{width:25px;height:4px;background-color:#5f6368;margin:3px 0;margin-right:6px;transition:.4s}.change .bar1{-webkit-transform:rotate(-45deg) translate(-5px,5px);transform:rotate(-45deg) translate(-5px,5px)}.change .bar2{opacity:0}.change .bar3{-webkit-transform:rotate(45deg) translate(-5px,-5px);transform:rotate(45deg) translate(-5px,-5px)}';"
+"document.body.appendChild(nodeStyle);"
+"console.log('CSS injected');"
+"var sdnv = document.querySelector(\".F-n-J\");var mnbtn = document.createElement(\"div\");var statusS=true;var nptsrc=document.querySelector('.h-n-j-Qc-lc');"
+"console.log('Variables created');"
+"var lrdnjct = document.createElement('div');lrdnjct.id='lrdnjct';document.body.appendChild(lrdnjct);"
+"waitForElementToDisplay(\".O-j\",function(){console.log(document.querySelector(\".O-j\"));"
+"mnbtn.id = 'mnbtn3';mnbtn.innerHTML = '<div class=\"bar1\"></div><div class=\"bar2\"></div><div class=\"bar3\"></div>';"
+"console.log('mnbtn initialized ');"
+"console.log(mnbtn);"
+"var mojbar = document.querySelector(\".O-j\");"
+"console.log(mojbar);"
+"mojbar.insertBefore(mnbtn, mojbar.firstChild);"
+"console.log('mnbtn inserted');"
+"console.log('add event listener to mnbtn');"
+"mnbtn.addEventListener('click',()=>{if(statusS){document.querySelector(\"#mnbtn3\").classList.add('change'); document.querySelector(\".F-n-J\").classList.add('showNav');}else{document.querySelector(\"#mnbtn3\").classList.remove('change'); document.querySelector(\".F-n-J\").classList.remove('showNav');}statusS=!statusS;});"
+"nptsrc.addEventListener('keypress',(evt)=>{if(evt.keyCode == 13){document.querySelector(\"#mnbtn3\").classList.remove('change'); document.querySelector(\".F-n-J\").classList.remove('showNav');statusS=true;}});"
+"console.log(\"Inject the elements\");},1000,20000);"
+"}else{console.log(\"CWS Mobile Script already injected\");}"
+"}); })();";
}
