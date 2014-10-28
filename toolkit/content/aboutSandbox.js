/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

'use strict';

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");

const localeStrings = Services.strings.createBundle(
    "chrome://global/locale/aboutSandbox.properties");

function PopulateTable() {
    let sysInfo = Cc["@mozilla.org/system-info;1"].getService(Ci.nsIPropertyBag2);
    let table = document.getElementById("sandboxFeatures");
    let rows = table.getElementsByTagName("tr");
    for (let i = 0; i < rows.length; ++i) {
	let row = rows[i];
	let key = row.dataset.feature;
	let value;
	try {
	    value = sysInfo.getProperty(key) ? "yes" : "no";
	} catch(e) {
	    row.style.display = "none";
	    continue;
	}
	let valueCell = document.createElement("td");
	valueCell.innerHTML = localeStrings.GetStringFromName(value);
	valueCell.classList.add(value);
	row.appendChild(valueCell);
    }
}
