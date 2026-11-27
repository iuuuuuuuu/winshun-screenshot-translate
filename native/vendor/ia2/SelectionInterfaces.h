#pragma once

/*************************************************************************
 *
 *  File Name (AccessibleText.idl)
 *
 *  IAccessible2 IDL Specification
 *
 *  Copyright (c) 2007, 2013 Linux Foundation
 *  Copyright (c) 2006 IBM Corporation
 *  Copyright (c) 2000, 2006 Sun Microsystems, Inc.
 *  All rights reserved.
 *
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 *   3. Neither the name of the Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  This BSD License conforms to the Open Source Initiative "Simplified
 *  BSD License" as published at:
 *  http://www.opensource.org/licenses/bsd-license.php
 *
 *  IAccessible2 is a trademark of the Linux Foundation. The IAccessible2
 *  mark may be used in accordance with the Linux Foundation Trademark
 *  Policy to indicate compliance with the IAccessible2 specification.
 *
 ************************************************************************/



// C++ COM declarations generated from LinuxA11y/IAccessible2 api IDLs with Windows SDK MIDL.
// Unused C bindings and RPC stubs omitted; method order and UUIDs are unchanged.
#include <oleacc.h>
#include <rpc.h>

enum IA2CoordinateType
    {
        IA2_COORDTYPE_SCREEN_RELATIVE	= 0,
        IA2_COORDTYPE_PARENT_RELATIVE	= ( IA2_COORDTYPE_SCREEN_RELATIVE + 1 )
    } ;

enum IA2ScrollType
    {
        IA2_SCROLL_TYPE_TOP_LEFT	= 0,
        IA2_SCROLL_TYPE_BOTTOM_RIGHT	= ( IA2_SCROLL_TYPE_TOP_LEFT + 1 ) ,
        IA2_SCROLL_TYPE_TOP_EDGE	= ( IA2_SCROLL_TYPE_BOTTOM_RIGHT + 1 ) ,
        IA2_SCROLL_TYPE_BOTTOM_EDGE	= ( IA2_SCROLL_TYPE_TOP_EDGE + 1 ) ,
        IA2_SCROLL_TYPE_LEFT_EDGE	= ( IA2_SCROLL_TYPE_BOTTOM_EDGE + 1 ) ,
        IA2_SCROLL_TYPE_RIGHT_EDGE	= ( IA2_SCROLL_TYPE_LEFT_EDGE + 1 ) ,
        IA2_SCROLL_TYPE_ANYWHERE	= ( IA2_SCROLL_TYPE_RIGHT_EDGE + 1 )
    } ;

typedef struct IA2TextSegment
    {
    BSTR text;
    long start;
    long end;
    } 	IA2TextSegment;

enum IA2TextBoundaryType
    {
        IA2_TEXT_BOUNDARY_CHAR	= 0,
        IA2_TEXT_BOUNDARY_WORD	= ( IA2_TEXT_BOUNDARY_CHAR + 1 ) ,
        IA2_TEXT_BOUNDARY_SENTENCE	= ( IA2_TEXT_BOUNDARY_WORD + 1 ) ,
        IA2_TEXT_BOUNDARY_PARAGRAPH	= ( IA2_TEXT_BOUNDARY_SENTENCE + 1 ) ,
        IA2_TEXT_BOUNDARY_LINE	= ( IA2_TEXT_BOUNDARY_PARAGRAPH + 1 ) ,
        IA2_TEXT_BOUNDARY_ALL	= ( IA2_TEXT_BOUNDARY_LINE + 1 )
    } ;

MIDL_INTERFACE("B70D9F59-3B5A-4dba-AB9E-22012F607DF5")
    IAccessibleAction : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE nActions(
             long *nActions) = 0;

        virtual HRESULT STDMETHODCALLTYPE doAction(
             long actionIndex) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_description(
             long actionIndex,
             BSTR *description) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_keyBinding(
             long actionIndex,
             long nMaxBindings,
             BSTR **keyBindings,
             long *nBindings) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_name(
             long actionIndex,
             BSTR *name) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_localizedName(
             long actionIndex,
             BSTR *localizedName) = 0;

    };

MIDL_INTERFACE("24FD2FFB-3AAD-4a08-8335-A3AD89C0FB4B")
    IAccessibleText : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE addSelection(
             long startOffset,
             long endOffset) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_attributes(
             long offset,
             long *startOffset,
             long *endOffset,
             BSTR *textAttributes) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_caretOffset(
             long *offset) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_characterExtents(
             long offset,
             enum IA2CoordinateType coordType,
             long *x,
             long *y,
             long *width,
             long *height) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_nSelections(
             long *nSelections) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_offsetAtPoint(
             long x,
             long y,
             enum IA2CoordinateType coordType,
             long *offset) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_selection(
             long selectionIndex,
             long *startOffset,
             long *endOffset) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_text(
             long startOffset,
             long endOffset,
             BSTR *text) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_textBeforeOffset(
             long offset,
             enum IA2TextBoundaryType boundaryType,
             long *startOffset,
             long *endOffset,
             BSTR *text) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_textAfterOffset(
             long offset,
             enum IA2TextBoundaryType boundaryType,
             long *startOffset,
             long *endOffset,
             BSTR *text) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_textAtOffset(
             long offset,
             enum IA2TextBoundaryType boundaryType,
             long *startOffset,
             long *endOffset,
             BSTR *text) = 0;

        virtual HRESULT STDMETHODCALLTYPE removeSelection(
             long selectionIndex) = 0;

        virtual HRESULT STDMETHODCALLTYPE setCaretOffset(
             long offset) = 0;

        virtual HRESULT STDMETHODCALLTYPE setSelection(
             long selectionIndex,
             long startOffset,
             long endOffset) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_nCharacters(
             long *nCharacters) = 0;

        virtual HRESULT STDMETHODCALLTYPE scrollSubstringTo(
             long startIndex,
             long endIndex,
             enum IA2ScrollType scrollType) = 0;

        virtual HRESULT STDMETHODCALLTYPE scrollSubstringToPoint(
             long startIndex,
             long endIndex,
             enum IA2CoordinateType coordinateType,
             long x,
             long y) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_newText(
             IA2TextSegment *newText) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_oldText(
             IA2TextSegment *oldText) = 0;

    };

MIDL_INTERFACE("01C20F2B-3DD2-400f-949F-AD00BDAB1D41")
    IAccessibleHyperlink : public IAccessibleAction
    {
    public:
        virtual  HRESULT STDMETHODCALLTYPE get_anchor(
             long index,
             VARIANT *anchor) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_anchorTarget(
             long index,
             VARIANT *anchorTarget) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_startIndex(
             long *index) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_endIndex(
             long *index) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_valid(
             boolean *valid) = 0;

    };

MIDL_INTERFACE("6B4F8BBF-F1F2-418a-B35E-A195BC4103B9")
    IAccessibleHypertext : public IAccessibleText
    {
    public:
        virtual  HRESULT STDMETHODCALLTYPE get_nHyperlinks(
             long *hyperlinkCount) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_hyperlink(
             long index,
             IAccessibleHyperlink **hyperlink) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_hyperlinkIndex(
             long charIndex,
             long *hyperlinkIndex) = 0;

    };

struct IAccessibleRelation;
typedef LONGLONG AccessibleStates;
typedef struct IA2Locale
    {
    BSTR language;
    BSTR country;
    BSTR variant;
    } 	IA2Locale;
MIDL_INTERFACE("E89F726E-C4F4-4c19-BB19-B647D7FA8478")
    IAccessible2 : public IAccessible
    {
    public:
        virtual  HRESULT STDMETHODCALLTYPE get_nRelations(
             long *nRelations) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_relation(
             long relationIndex,
             IAccessibleRelation **relation) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_relations(
             long maxRelations,
             IAccessibleRelation **relations,
             long *nRelations) = 0;

        virtual HRESULT STDMETHODCALLTYPE role(
             long *role) = 0;

        virtual HRESULT STDMETHODCALLTYPE scrollTo(
             enum IA2ScrollType scrollType) = 0;

        virtual HRESULT STDMETHODCALLTYPE scrollToPoint(
             enum IA2CoordinateType coordinateType,
             long x,
             long y) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_groupPosition(
             long *groupLevel,
             long *similarItemsInGroup,
             long *positionInGroup) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_states(
             AccessibleStates *states) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_extendedRole(
             BSTR *extendedRole) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_localizedExtendedRole(
             BSTR *localizedExtendedRole) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_nExtendedStates(
             long *nExtendedStates) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_extendedStates(
             long maxExtendedStates,
             BSTR **extendedStates,
             long *nExtendedStates) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_localizedExtendedStates(
             long maxLocalizedExtendedStates,
             BSTR **localizedExtendedStates,
             long *nLocalizedExtendedStates) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_uniqueID(
             long *uniqueID) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_windowHandle(
             HWND *windowHandle) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_indexInParent(
             long *indexInParent) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_locale(
             IA2Locale *locale) = 0;

        virtual  HRESULT STDMETHODCALLTYPE get_attributes(
             BSTR *attributes) = 0;

    };
