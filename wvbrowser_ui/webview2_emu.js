
if (!window.chrome) {
    console.warn("WINDOW.CHROME not available. Emulating...");
    window.chrome = {
    };
}
if (!window.chrome.webview) {
    console.warn("WINDOW.CHROME.WEBVIEW not available. Emulating...");
    window.chrome.webview = {
        postMessage: function (message) {
            console.info("WEBVIEW.POSTMESSAGE:", message);
        },
        addEventListener: function (eventName, messageHandler) {
            console.info("WEBVIEW.addEventListener:", { eventName, messageHandler });
        }
    };
}
