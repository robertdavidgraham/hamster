function refresh_xml(url, handle_response)
{
	xmlhttp=null;

	if (window.XMLHttpRequest) {
		// code for IE7, Firefox, Mozilla, etc.
		xmlhttp=new XMLHttpRequest();
	} else if (window.ActiveXObject) {
		// code for IE5, IE6
		xmlhttp=new ActiveXObject("Microsoft.XMLHTTP");
	}

	if (xmlhttp != null) {
		xmlhttp.onreadystatechange = handle_response;
		xmlhttp.open("GET",url,true);
		xmlhttp.send(null);
	}

}

var last_readystate = 4;
var last_instance_count = 0;

function refresh_status()
{
	if (last_readystate != 4) {
		set_status(0, "Hamster Proxy crashed or disconnected, err(readystate=" + last_readystate + ")");
	}
	refresh_xml("status.xml", handle_status);
}


function set_status(code, message, adapters)
{
	var div = document.getElementById("hamsterstatus")
	if (!div)
		return;

	if (code == 0 || code == "0")
		div.className = "statuserror";
	else
		div.className = "statusok";

	div.innerHTML = message;
}

function handle_status()
{
	var update;
	var message;

	last_readystate = xmlhttp.readyState;

	// Wait until we get a reply back from the server		
	if(xmlhttp.readyState != 4) {
		return;
	}

	// Make sure the reply is "HTTP/1.1 200 OK"
	if(xmlhttp.status != 200) {
		set_status(0, "Hamster Proxy crashed or disconnected, err(httpcode=" + xmlhttp.status + ")");
		return;
	}

	//
	update = xmlhttp.responseXML.documentElement;
	if (!update) {
		set_status(0, "Error talking to Hamster proxy");
		return;
	}

	if (update.nodeName != "hamsterstatus") {
		set_status(0, "Corrupted data from Hamster proxy");
		return; // corrupt XML contents
	
	}

	message = update.getElementsByTagName("message")[0].firstChild.nodeValue;
	code = update.getElementsByTagName("code")[0].firstChild.nodeValue;

	set_status(code, message);

	/*
	 * Adapters field
	 */
	var adapters = update.getElementsByTagName("adapters")[0].firstChild.nodeValue;
	var field = document.getElementById("adapterstatus")

	if (adapters == "none" || adapters == "none ")
		field.className = "statuserror";
	else
		field.className = "statusok";
	field.innerHTML = adapters;

	/*
	 * Database records field
	 */
	var recordcount = update.getElementsByTagName("recordcount")[0].firstChild.nodeValue;
	field = document.getElementById("dbstatus")

	if (recordcount == "0")
		field.className = "statuserror";
	else
		field.className = "statusok";
	field.innerHTML = recordcount;

	/*
	 * Packet status field
	 */
	var packetcount = update.getElementsByTagName("packetcount")[0].firstChild.nodeValue;
	field = document.getElementById("packetstatus")

	if (recordcount == "0")
		field.className = "statuserror";
	else
		field.className = "statusok";
	field.innerHTML = packetcount;

	/*
	 * Target count
	 */
	var targetcount = update.getElementsByTagName("targetcount")[0].firstChild.nodeValue;
	field = document.getElementById("targetstatus")

	if (targetcount == "0")
		field.className = "statuserror";
	else
		field.className = "statusok";
	field.innerHTML = targetcount;

	if (parseInt(targetcount) != last_instance_count) {
		last_instance_count = parseInt(targetcount);
		location.reload(1);
	}


}



