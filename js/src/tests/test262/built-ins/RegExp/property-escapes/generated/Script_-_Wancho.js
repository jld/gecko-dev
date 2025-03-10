// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script=Wancho`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v16.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x01E2FF
  ],
  ranges: [
    [0x01E2C0, 0x01E2F9]
  ]
});
testPropertyEscapes(
  /^\p{Script=Wancho}+$/u,
  matchSymbols,
  "\\p{Script=Wancho}"
);
testPropertyEscapes(
  /^\p{Script=Wcho}+$/u,
  matchSymbols,
  "\\p{Script=Wcho}"
);
testPropertyEscapes(
  /^\p{sc=Wancho}+$/u,
  matchSymbols,
  "\\p{sc=Wancho}"
);
testPropertyEscapes(
  /^\p{sc=Wcho}+$/u,
  matchSymbols,
  "\\p{sc=Wcho}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00DBFF],
    [0x00E000, 0x01E2BF],
    [0x01E2FA, 0x01E2FE],
    [0x01E300, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script=Wancho}+$/u,
  nonMatchSymbols,
  "\\P{Script=Wancho}"
);
testPropertyEscapes(
  /^\P{Script=Wcho}+$/u,
  nonMatchSymbols,
  "\\P{Script=Wcho}"
);
testPropertyEscapes(
  /^\P{sc=Wancho}+$/u,
  nonMatchSymbols,
  "\\P{sc=Wancho}"
);
testPropertyEscapes(
  /^\P{sc=Wcho}+$/u,
  nonMatchSymbols,
  "\\P{sc=Wcho}"
);

reportCompare(0, 0);
