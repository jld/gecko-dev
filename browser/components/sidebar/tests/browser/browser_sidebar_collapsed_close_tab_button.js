/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.verticalTabs", true],
      ["sidebar.visibility", "always-show"],
    ],
  });
});
registerCleanupFunction(async () => {
  await SpecialPowers.popPrefEnv();
  gBrowser.removeAllTabsBut(gBrowser.tabs[0]);
});

add_task(async function test_toggle_collapse_close_button() {
  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");

  if (window.SidebarController._state.launcherExpanded) {
    await SidebarController.initializeUIState({ launcherExpanded: false });
    await sidebar.updateComplete;
  }
  ok(!sidebar.expanded, "Sidebar is collapsed by default.");

  let newTabButton = document.getElementById("tabs-newtab-button");
  info("Open a new tab using the new tab button.");
  EventUtils.synthesizeMouseAtCenter(newTabButton, {});
  is(gBrowser.tabs.length, 2, "Tabstrip now has two tabs");

  let firstTab = gBrowser.visibleTabs[0];
  let selectedTab = gBrowser.selectedTab.querySelector(".tab-close-button");
  let computedStyle = window.getComputedStyle(selectedTab);

  is(
    computedStyle.opacity,
    "0",
    "The selected tab is not showing the close button."
  );

  EventUtils.synthesizeMouse(gBrowser.selectedTab, 10, 10, {
    type: "mouseover",
  });
  await TestUtils.waitForTick();

  computedStyle = window.getComputedStyle(selectedTab);
  is(
    computedStyle.opacity,
    "1",
    "The selected tab is showing the close button on hover."
  );

  gBrowser.pinTab(gBrowser.selectedTab);
  computedStyle = window.getComputedStyle(selectedTab);

  is(
    computedStyle.opacity,
    "0",
    "The active pinned tab is not showing the close button."
  );

  EventUtils.synthesizeMouse(gBrowser.selectedTab, 10, 10, {
    type: "mouseover",
  });
  computedStyle = window.getComputedStyle(selectedTab);
  is(
    computedStyle.opacity,
    "0",
    "The inactive pinned tab is not showing the close button on hover."
  );
  gBrowser.unpinTab(gBrowser.selectedTab);

  // Move mouse away from tabstrip to ensure we don't show the close button.
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("tabbrowser-tabbox"),
    { type: "mouseover" }
  );
  await TestUtils.waitForTick();

  computedStyle = window.getComputedStyle(
    firstTab.querySelector(".tab-close-button")
  );
  is(
    computedStyle.opacity,
    "0",
    "The inactive tab is not showing the close button."
  );

  // Check that collapsed close button is shown on hover of the inactive tab
  EventUtils.synthesizeMouse(firstTab, 10, 10, { type: "mouseover" });
  computedStyle = window.getComputedStyle(
    firstTab.querySelector(".tab-close-button")
  );
  is(
    computedStyle.opacity,
    "1",
    "The inactive tab is showing the close button on hover."
  );
  // The tab can be closed via the keyboard shortcut, this button is not focusable
  AccessibilityUtils.setEnv({ focusableRule: false });
  // Close the active tab
  EventUtils.synthesizeMouseAtCenter(
    firstTab.querySelector(".tab-close-button"),
    {}
  );
  AccessibilityUtils.resetEnv();
  is(gBrowser.tabs.length, 1, "Tabstrip now has one tab");

  // Expand the sidebar and make sure the collased close button no longer shows
  await SidebarController.initializeUIState({ launcherExpanded: true });
  await sidebar.updateComplete;
  await TestUtils.waitForCondition(() => {
    return window.SidebarController._state.launcherExpanded;
  }, "Sidebar launcher is expanded");
  computedStyle = window.getComputedStyle(
    gBrowser.selectedTab.querySelector(".tab-close-button")
  );
  is(
    computedStyle.position,
    "static",
    "The active tab is showing the collapsed close button when the sidebar is expanded."
  );
});
