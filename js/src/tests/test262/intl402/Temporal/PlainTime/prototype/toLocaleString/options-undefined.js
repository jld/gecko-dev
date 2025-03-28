// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaintime.prototype.tolocalestring
description: Verify that undefined options are handled correctly.
features: [Temporal]
---*/

const time = new Temporal.PlainTime(12, 34, 56, 987, 654, 321);
const defaultFormatter = new Intl.DateTimeFormat('en', Object.create(null));
const expected = defaultFormatter.format(time);

const actualExplicit = time.toLocaleString('en', undefined);
assert.sameValue(actualExplicit, expected, "default options are determined by Intl.DateTimeFormat");

const actualImplicit = time.toLocaleString('en');
assert.sameValue(actualImplicit, expected, "default options are determined by Intl.DateTimeFormat");

reportCompare(0, 0);
