/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const TEST_PAGE =
  "http://mochi.test:8888/browser/dom/ipc/tests/file_dummy.html";

const NUM_PROCS = 4096;
const NUM_TABS = 16;

requestLongerTimeout(NUM_PROCS / 400);

add_task(async () => {
  for (let i = 0; i < NUM_PROCS; i += NUM_TABS) {
    ok(true, "Progress: " + i + " processes");
    let promises = [];

    for (let j = 0; j < NUM_TABS; ++j) {
      promises.push(
        BrowserTestUtils.openNewForegroundTab({
          gBrowser,
          opening: TEST_PAGE,
          waitForLoad: true,
          forceNewProcess: true,
        })
      );
    }

    let tabs = [];
    for (const p of promises) {
      tabs.push(await p);
    }
    for (const t of tabs) {
      BrowserTestUtils.removeTab(t);
    }
  }

  ok(true, "Successfully started/stopped " + NUM_PROCS + " processes");
});
