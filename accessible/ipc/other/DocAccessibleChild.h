/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_DocAccessibleChild_h
#define mozilla_a11y_DocAccessibleChild_h

#include "mozilla/a11y/DocAccessibleChildBase.h"

namespace mozilla {
namespace a11y {

class Accessible;
class HyperTextAccessible;
class TextLeafAccessible;
class ImageAccessible;
class TableAccessible;
class TableCellAccessible;

/*
 * These objects handle content side communication for an accessible document,
 * and their lifetime is the same as the document they represent.
 */
class DocAccessibleChild : public DocAccessibleChildBase
{
public:
  DocAccessibleChild(DocAccessible* aDoc, IProtocol* aManager)
    : DocAccessibleChildBase(aDoc)
  {
    MOZ_COUNT_CTOR_INHERITED(DocAccessibleChild, DocAccessibleChildBase);
    SetManager(aManager);
  }

  ~DocAccessibleChild()
  {
    MOZ_COUNT_DTOR_INHERITED(DocAccessibleChild, DocAccessibleChildBase);
  }

  virtual mozilla::ipc::IPCResult RecvRestoreFocus() override;

  /*
   * Return the state for the accessible with given ID.
   */
  virtual mozilla::ipc::IPCResult RecvState(uint64_t&& aID, uint64_t* aState) override;

  /*
   * Return the native state for the accessible with given ID.
   */
  virtual mozilla::ipc::IPCResult RecvNativeState(uint64_t&& aID, uint64_t* aState) override;

  /*
   * Get the name for the accessible with given id.
   */
  virtual mozilla::ipc::IPCResult RecvName(uint64_t&& aID, nsString* aName) override;

  virtual mozilla::ipc::IPCResult RecvValue(uint64_t&& aID, nsString* aValue) override;

  virtual mozilla::ipc::IPCResult RecvHelp(uint64_t&& aID, nsString* aHelp) override;

  /*
   * Get the description for the accessible with given id.
   */
  virtual mozilla::ipc::IPCResult RecvDescription(uint64_t&& aID, nsString* aDesc) override;
  virtual mozilla::ipc::IPCResult RecvRelationByType(uint64_t&& aID, uint32_t&& aType,
                                                     nsTArray<uint64_t>* aTargets) override;
  virtual mozilla::ipc::IPCResult RecvRelations(uint64_t&& aID,
                                                nsTArray<RelationTargets>* aRelations)
    override;

  virtual mozilla::ipc::IPCResult RecvIsSearchbox(uint64_t&& aID, bool* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvLandmarkRole(uint64_t&& aID, nsString* aLandmark) override;

  virtual mozilla::ipc::IPCResult RecvARIARoleAtom(uint64_t&& aID, nsString* aRole) override;

  virtual mozilla::ipc::IPCResult RecvGetLevelInternal(uint64_t&& aID, int32_t* aLevel) override;

  virtual mozilla::ipc::IPCResult RecvAttributes(uint64_t&& aID,
                                                 nsTArray<Attribute> *aAttributes) override;
  virtual mozilla::ipc::IPCResult RecvScrollTo(uint64_t&& aID, uint32_t&& aScrollType)
    override;
  virtual mozilla::ipc::IPCResult RecvScrollToPoint(uint64_t&& aID,
                                                    uint32_t&& aScrollType,
                                                    int32_t&& aX, int32_t&& aY) override;

  virtual mozilla::ipc::IPCResult RecvCaretLineNumber(uint64_t&& aID, int32_t* aLineNumber)
    override;
  virtual mozilla::ipc::IPCResult RecvCaretOffset(uint64_t&& aID, int32_t* aOffset)
    override;
  virtual mozilla::ipc::IPCResult RecvSetCaretOffset(uint64_t&& aID, int32_t&& aOffset)
    override;

  virtual mozilla::ipc::IPCResult RecvCharacterCount(uint64_t&& aID, int32_t* aCount)
     override;
  virtual mozilla::ipc::IPCResult RecvSelectionCount(uint64_t&& aID, int32_t* aCount)
     override;

  virtual mozilla::ipc::IPCResult RecvTextSubstring(uint64_t&& aID,
                                                    int32_t&& aStartOffset,
                                                    int32_t&& aEndOffset, nsString* aText,
                                                    bool* aValid) override;

  virtual mozilla::ipc::IPCResult RecvGetTextAfterOffset(uint64_t&& aID,
                                                         int32_t&& aOffset,
                                                         int32_t&& aBoundaryType,
                                                         nsString* aText, int32_t* aStartOffset,
                                                         int32_t* aEndOffset) override;
  virtual mozilla::ipc::IPCResult RecvGetTextAtOffset(uint64_t&& aID,
                                                      int32_t&& aOffset,
                                                      int32_t&& aBoundaryType,
                                                      nsString* aText, int32_t* aStartOffset,
                                                      int32_t* aEndOffset) override;
  virtual mozilla::ipc::IPCResult RecvGetTextBeforeOffset(uint64_t&& aID,
                                                          int32_t&& aOffset,
                                                          int32_t&& aBoundaryType,
                                                          nsString* aText, int32_t* aStartOffset,
                                                          int32_t* aEndOffset) override;

  virtual mozilla::ipc::IPCResult RecvCharAt(uint64_t&& aID,
                                             int32_t&& aOffset,
                                             uint16_t* aChar) override;

  virtual mozilla::ipc::IPCResult RecvTextAttributes(uint64_t&& aID,
                                                     bool&& aIncludeDefAttrs,
                                                     int32_t&& aOffset,
                                                     nsTArray<Attribute>* aAttributes,
                                                     int32_t* aStartOffset,
                                                     int32_t* aEndOffset)
    override;

  virtual mozilla::ipc::IPCResult RecvDefaultTextAttributes(uint64_t&& aID,
                                                            nsTArray<Attribute>* aAttributes)
    override;

  virtual mozilla::ipc::IPCResult RecvTextBounds(uint64_t&& aID,
                                                 int32_t&& aStartOffset,
                                                 int32_t&& aEndOffset,
                                                 uint32_t&& aCoordType,
                                                 nsIntRect* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvCharBounds(uint64_t&& aID,
                                                 int32_t&& aOffset,
                                                 uint32_t&& aCoordType,
                                                 nsIntRect* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvOffsetAtPoint(uint64_t&& aID,
                                                    int32_t&& aX,
                                                    int32_t&& aY,
                                                    uint32_t&& aCoordType,
                                                    int32_t* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvSelectionBoundsAt(uint64_t&& aID,
                                                        int32_t&& aSelectionNum,
                                                        bool* aSucceeded,
                                                        nsString* aData,
                                                        int32_t* aStartOffset,
                                                        int32_t* aEndOffset) override;

  virtual mozilla::ipc::IPCResult RecvSetSelectionBoundsAt(uint64_t&& aID,
                                                           int32_t&& aSelectionNum,
                                                           int32_t&& aStartOffset,
                                                           int32_t&& aEndOffset,
                                                           bool* aSucceeded) override;

  virtual mozilla::ipc::IPCResult RecvAddToSelection(uint64_t&& aID,
                                                     int32_t&& aStartOffset,
                                                     int32_t&& aEndOffset,
                                                     bool* aSucceeded) override;

  virtual mozilla::ipc::IPCResult RecvRemoveFromSelection(uint64_t&& aID,
                                                          int32_t&& aSelectionNum,
                                                          bool* aSucceeded) override;

  virtual mozilla::ipc::IPCResult RecvScrollSubstringTo(uint64_t&& aID,
                                                        int32_t&& aStartOffset,
                                                        int32_t&& aEndOffset,
                                                        uint32_t&& aScrollType) override;

  virtual mozilla::ipc::IPCResult RecvScrollSubstringToPoint(uint64_t&& aID,
                                                             int32_t&& aStartOffset,
                                                             int32_t&& aEndOffset,
                                                             uint32_t&& aCoordinateType,
                                                             int32_t&& aX,
                                                             int32_t&& aY) override;

  virtual mozilla::ipc::IPCResult RecvText(uint64_t&& aID,
                                           nsString* aText) override;

  virtual mozilla::ipc::IPCResult RecvReplaceText(uint64_t&& aID,
                                                  nsString&& aText) override;

  virtual mozilla::ipc::IPCResult RecvInsertText(uint64_t&& aID,
                                                 nsString&& aText,
                                                 int32_t&& aPosition, bool* aValid) override;

  virtual mozilla::ipc::IPCResult RecvCopyText(uint64_t&& aID,
                                               int32_t&& aStartPos,
                                               int32_t&& aEndPos, bool* aValid) override;

  virtual mozilla::ipc::IPCResult RecvCutText(uint64_t&& aID,
                                              int32_t&& aStartPos,
                                              int32_t&& aEndPos, bool* aValid) override;

  virtual mozilla::ipc::IPCResult RecvDeleteText(uint64_t&& aID,
                                                 int32_t&& aStartPos,
                                                 int32_t&& aEndPos, bool* aValid) override;

  virtual mozilla::ipc::IPCResult RecvPasteText(uint64_t&& aID,
                                                int32_t&& aPosition, bool* aValid) override;

  virtual mozilla::ipc::IPCResult RecvImagePosition(uint64_t&& aID,
                                                    uint32_t&& aCoordType,
                                                    nsIntPoint* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvImageSize(uint64_t&& aID,
                                                nsIntSize* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvStartOffset(uint64_t&& aID,
                                                  uint32_t* aRetVal,
                                                  bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvEndOffset(uint64_t&& aID,
                                                uint32_t* aRetVal,
                                                bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvIsLinkValid(uint64_t&& aID,
                                                  bool* aRetVal) override;
  virtual mozilla::ipc::IPCResult RecvAnchorCount(uint64_t&& aID,
                                                  uint32_t* aRetVal, bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvAnchorURIAt(uint64_t&& aID,
                                                  uint32_t&& aIndex,
                                                  nsCString* aURI,
                                                  bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvAnchorAt(uint64_t&& aID,
                                               uint32_t&& aIndex,
                                               uint64_t* aIDOfAnchor,
                                               bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvLinkCount(uint64_t&& aID,
                                                uint32_t* aCount) override;

  virtual mozilla::ipc::IPCResult RecvLinkAt(uint64_t&& aID,
                                             uint32_t&& aIndex,
                                             uint64_t* aIDOfLink,
                                             bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvLinkIndexOf(uint64_t&& aID,
                                                  uint64_t&& aLinkID,
                                                  int32_t* aIndex) override;

  virtual mozilla::ipc::IPCResult RecvLinkIndexAtOffset(uint64_t&& aID,
                                                        uint32_t&& aOffset,
                                                        int32_t* aIndex) override;

  virtual mozilla::ipc::IPCResult RecvTableOfACell(uint64_t&& aID,
                                                   uint64_t* aTableID,
                                                   bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvColIdx(uint64_t&& aID, uint32_t* aIndex) override;

  virtual mozilla::ipc::IPCResult RecvRowIdx(uint64_t&& aID, uint32_t* aIndex) override;

  virtual mozilla::ipc::IPCResult RecvColExtent(uint64_t&& aID, uint32_t* aExtent) override;

  virtual mozilla::ipc::IPCResult RecvGetPosition(uint64_t&& aID,
                                                  uint32_t* aColIdx, uint32_t* aRowIdx) override;

  virtual mozilla::ipc::IPCResult RecvGetColRowExtents(uint64_t&& aID,
                                                       uint32_t* aColIdx, uint32_t* aRowIdx,
                                                       uint32_t* aColExtent, uint32_t* aRowExtent) override;

  virtual mozilla::ipc::IPCResult RecvRowExtent(uint64_t&& aID, uint32_t* aExtent) override;

  virtual mozilla::ipc::IPCResult RecvColHeaderCells(uint64_t&& aID,
                                                     nsTArray<uint64_t>* aCells) override;

  virtual mozilla::ipc::IPCResult RecvRowHeaderCells(uint64_t&& aID,
                                                     nsTArray<uint64_t>* aCells) override;

  virtual mozilla::ipc::IPCResult RecvIsCellSelected(uint64_t&& aID,
                                                     bool* aSelected) override;

  virtual mozilla::ipc::IPCResult RecvTableCaption(uint64_t&& aID,
                                                   uint64_t* aCaptionID,
                                                   bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvTableSummary(uint64_t&& aID,
                                                   nsString* aSummary) override;
  virtual mozilla::ipc::IPCResult RecvTableColumnCount(uint64_t&& aID,
                                                       uint32_t* aColCount) override;
  virtual mozilla::ipc::IPCResult RecvTableRowCount(uint64_t&& aID,
                                                    uint32_t* aRowCount) override;
  virtual mozilla::ipc::IPCResult RecvTableCellAt(uint64_t&& aID,
                                                  uint32_t&& aRow,
                                                  uint32_t&& aCol,
                                                  uint64_t* aCellID,
                                                  bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvTableCellIndexAt(uint64_t&& aID,
                                                       uint32_t&& aRow,
                                                       uint32_t&& aCol,
                                                       int32_t* aIndex) override;
  virtual mozilla::ipc::IPCResult RecvTableColumnIndexAt(uint64_t&& aID,
                                                         uint32_t&& aCellIndex,
                                                         int32_t* aCol) override;
  virtual mozilla::ipc::IPCResult RecvTableRowIndexAt(uint64_t&& aID,
                                                      uint32_t&& aCellIndex,
                                                      int32_t* aRow) override;
  virtual mozilla::ipc::IPCResult RecvTableRowAndColumnIndicesAt(uint64_t&& aID,
                                                                 uint32_t&& aCellIndex,
                                                                 int32_t* aRow,
                                                                 int32_t* aCol) override;
  virtual mozilla::ipc::IPCResult RecvTableColumnExtentAt(uint64_t&& aID,
                                                          uint32_t&& aRow,
                                                          uint32_t&& aCol,
                                                          uint32_t* aExtent) override;
  virtual mozilla::ipc::IPCResult RecvTableRowExtentAt(uint64_t&& aID,
                                                       uint32_t&& aRow,
                                                       uint32_t&& aCol,
                                                       uint32_t* aExtent) override;
  virtual mozilla::ipc::IPCResult RecvTableColumnDescription(uint64_t&& aID,
                                                             uint32_t&& aCol,
                                                             nsString* aDescription) override;
  virtual mozilla::ipc::IPCResult RecvTableRowDescription(uint64_t&& aID,
                                                          uint32_t&& aRow,
                                                          nsString* aDescription) override;
  virtual mozilla::ipc::IPCResult RecvTableColumnSelected(uint64_t&& aID,
                                                          uint32_t&& aCol,
                                                          bool* aSelected) override;
  virtual mozilla::ipc::IPCResult RecvTableRowSelected(uint64_t&& aID,
                                                       uint32_t&& aRow,
                                                       bool* aSelected) override;
  virtual mozilla::ipc::IPCResult RecvTableCellSelected(uint64_t&& aID,
                                                        uint32_t&& aRow,
                                                        uint32_t&& aCol,
                                                        bool* aSelected) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedCellCount(uint64_t&& aID,
                                                             uint32_t* aSelectedCells) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedColumnCount(uint64_t&& aID,
                                                               uint32_t* aSelectedColumns) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedRowCount(uint64_t&& aID,
                                                            uint32_t* aSelectedRows) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedCells(uint64_t&& aID,
                                                         nsTArray<uint64_t>* aCellIDs) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedCellIndices(uint64_t&& aID,
                                                               nsTArray<uint32_t>* aCellIndices) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedColumnIndices(uint64_t&& aID,
                                                                 nsTArray<uint32_t>* aColumnIndices) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectedRowIndices(uint64_t&& aID,
                                                              nsTArray<uint32_t>* aRowIndices) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectColumn(uint64_t&& aID,
                                                        uint32_t&& aCol) override;
  virtual mozilla::ipc::IPCResult RecvTableSelectRow(uint64_t&& aID,
                                                     uint32_t&& aRow) override;
  virtual mozilla::ipc::IPCResult RecvTableUnselectColumn(uint64_t&& aID,
                                                          uint32_t&& aCol) override;
  virtual mozilla::ipc::IPCResult RecvTableUnselectRow(uint64_t&& aID,
                                                       uint32_t&& aRow) override;
  virtual mozilla::ipc::IPCResult RecvTableIsProbablyForLayout(uint64_t&& aID,
                                                               bool* aForLayout) override;
  virtual mozilla::ipc::IPCResult RecvAtkTableColumnHeader(uint64_t&& aID,
                                                           int32_t&& aCol,
                                                           uint64_t* aHeader,
                                                           bool* aOk) override;
  virtual mozilla::ipc::IPCResult RecvAtkTableRowHeader(uint64_t&& aID,
                                                        int32_t&& aRow,
                                                        uint64_t* aHeader,
                                                        bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvSelectedItems(uint64_t&& aID,
                                                    nsTArray<uint64_t>* aSelectedItemIDs) override;

  virtual mozilla::ipc::IPCResult RecvSelectedItemCount(uint64_t&& aID,
                                                        uint32_t* aCount) override;

  virtual mozilla::ipc::IPCResult RecvGetSelectedItem(uint64_t&& aID,
                                                      uint32_t&& aIndex,
                                                      uint64_t* aSelected,
                                                      bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvIsItemSelected(uint64_t&& aID,
                                                     uint32_t&& aIndex,
                                                     bool* aSelected) override;

  virtual mozilla::ipc::IPCResult RecvAddItemToSelection(uint64_t&& aID,
                                                         uint32_t&& aIndex,
                                                         bool* aSuccess) override;

  virtual mozilla::ipc::IPCResult RecvRemoveItemFromSelection(uint64_t&& aID,
                                                              uint32_t&& aIndex,
                                                              bool* aSuccess) override;

  virtual mozilla::ipc::IPCResult RecvSelectAll(uint64_t&& aID,
                                                bool* aSuccess) override;

  virtual mozilla::ipc::IPCResult RecvUnselectAll(uint64_t&& aID,
                                                  bool* aSuccess) override;

  virtual mozilla::ipc::IPCResult RecvTakeSelection(uint64_t&& aID) override;
  virtual mozilla::ipc::IPCResult RecvSetSelected(uint64_t&& aID,
                                                  bool&& aSelect) override;

  virtual mozilla::ipc::IPCResult RecvDoAction(uint64_t&& aID,
                                               uint8_t&& aIndex,
                                               bool* aSuccess) override;

  virtual mozilla::ipc::IPCResult RecvActionCount(uint64_t&& aID,
                                                  uint8_t* aCount) override;

  virtual mozilla::ipc::IPCResult RecvActionDescriptionAt(uint64_t&& aID,
                                                          uint8_t&& aIndex,
                                                          nsString* aDescription) override;

  virtual mozilla::ipc::IPCResult RecvActionNameAt(uint64_t&& aID,
                                                   uint8_t&& aIndex,
                                                   nsString* aName) override;

  virtual mozilla::ipc::IPCResult RecvAccessKey(uint64_t&& aID,
                                                uint32_t* aKey,
                                                uint32_t* aModifierMask) override;

  virtual mozilla::ipc::IPCResult RecvKeyboardShortcut(uint64_t&& aID,
                                                       uint32_t* aKey,
                                                       uint32_t* aModifierMask) override;

  virtual mozilla::ipc::IPCResult RecvAtkKeyBinding(uint64_t&& aID,
                                                    nsString* aResult) override;

  virtual mozilla::ipc::IPCResult RecvCurValue(uint64_t&& aID,
                                               double* aValue) override;

  virtual mozilla::ipc::IPCResult RecvSetCurValue(uint64_t&& aID,
                                                  double&& aValue,
                                                  bool* aRetVal) override;

  virtual mozilla::ipc::IPCResult RecvMinValue(uint64_t&& aID,
                                               double* aValue) override;

  virtual mozilla::ipc::IPCResult RecvMaxValue(uint64_t&& aID,
                                               double* aValue) override;

  virtual mozilla::ipc::IPCResult RecvStep(uint64_t&& aID,
                                           double* aStep) override;

  virtual mozilla::ipc::IPCResult RecvTakeFocus(uint64_t&& aID) override;

  virtual mozilla::ipc::IPCResult RecvFocusedChild(uint64_t&& aID,
                                                   uint64_t* aChild,
                                                   bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvLanguage(uint64_t&& aID, nsString* aLocale) override;
  virtual mozilla::ipc::IPCResult RecvDocType(uint64_t&& aID, nsString* aType) override;
  virtual mozilla::ipc::IPCResult RecvTitle(uint64_t&& aID, nsString* aTitle) override;
  virtual mozilla::ipc::IPCResult RecvURL(uint64_t&& aID, nsString* aURL) override;
  virtual mozilla::ipc::IPCResult RecvMimeType(uint64_t&& aID, nsString* aMime) override;
  virtual mozilla::ipc::IPCResult RecvURLDocTypeMimeType(uint64_t&& aID,
                                                         nsString* aURL,
                                                         nsString* aDocType,
                                                         nsString* aMimeType) override;

  virtual mozilla::ipc::IPCResult RecvAccessibleAtPoint(uint64_t&& aID,
                                                        int32_t&& aX,
                                                        int32_t&& aY,
                                                        bool&& aNeedsScreenCoords,
                                                        uint32_t&& aWhich,
                                                        uint64_t* aResult,
                                                        bool* aOk) override;

  virtual mozilla::ipc::IPCResult RecvExtents(uint64_t&& aID,
                                              bool&& aNeedsScreenCoords,
                                              int32_t* aX,
                                              int32_t* aY,
                                              int32_t* aWidth,
                                              int32_t* aHeight) override;
  virtual mozilla::ipc::IPCResult RecvExtentsInCSSPixels(uint64_t&& aID,
                                                         int32_t* aX,
                                                         int32_t* aY,
                                                         int32_t* aWidth,
                                                         int32_t* aHeight) override;
  virtual mozilla::ipc::IPCResult RecvDOMNodeID(uint64_t&& aID, nsString* aDOMNodeID) override;
private:

  Accessible* IdToAccessible(const uint64_t& aID) const;
  Accessible* IdToAccessibleLink(const uint64_t& aID) const;
  Accessible* IdToAccessibleSelect(const uint64_t& aID) const;
  HyperTextAccessible* IdToHyperTextAccessible(const uint64_t& aID) const;
  TextLeafAccessible* IdToTextLeafAccessible(const uint64_t& aID) const;
  ImageAccessible* IdToImageAccessible(const uint64_t& aID) const;
  TableCellAccessible* IdToTableCellAccessible(const uint64_t& aID) const;
  TableAccessible* IdToTableAccessible(const uint64_t& aID) const;

  bool PersistentPropertiesToArray(nsIPersistentProperties* aProps,
                                   nsTArray<Attribute>* aAttributes);
};

}
}

#endif
