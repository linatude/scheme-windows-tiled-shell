 // injected when documents load; to provide scheme execution of selected text.
function evaluate_selected_expression() {
	var text = window.getSelection().toString();
	text = text.replace(/\u00a0/g, " ");
	text = text.replace(/[\u{0080}-\u{FFFF}]/gu, "");
	window.chrome.webview.postMessage('::eval:' + text);
}
window.addEventListener('keydown', function (event) {
	if(event.code == 'Enter' && (event.shiftKey)) {
	evaluate_selected_expression();
	}
});

window.chrome.webview.addEventListener('message', event => handle_events(event.data));

 function handle_events(data) {
	 console.log(data);
 }
 
 