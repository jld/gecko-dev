/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocument.h"
#include "nsWebBrowserPersistDocumentParent.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLSharedElement.h"
#include "mozilla/dom/HTMLSharedObjectElement.h"
#include "mozilla/dom/TabParent.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsContentCID.h"
#include "nsIComponentRegistrar.h"
#include "nsIContent.h"
#include "nsIDOMAttr.h"
#include "nsIDOMComment.h"
#include "nsIDOMDocument.h"
#include "nsIDOMHTMLAnchorElement.h"
#include "nsIDOMHTMLAppletElement.h"
#include "nsIDOMHTMLAreaElement.h"
#include "nsIDOMHTMLBaseElement.h"
#include "nsIDOMHTMLCollection.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLEmbedElement.h"
#include "nsIDOMHTMLFrameElement.h"
#include "nsIDOMHTMLIFrameElement.h"
#include "nsIDOMHTMLImageElement.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIDOMHTMLLinkElement.h"
#include "nsIDOMHTMLMediaElement.h"
#include "nsIDOMHTMLObjectElement.h"
#include "nsIDOMHTMLOptionElement.h"
#include "nsIDOMHTMLScriptElement.h"
#include "nsIDOMHTMLSourceElement.h"
#include "nsIDOMHTMLTextAreaElement.h"
#include "nsIDOMMozNamedAttrMap.h"
#include "nsIDOMNode.h"
#include "nsIDOMNodeFilter.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMProcessingInstruction.h"
#include "nsIDOMTreeWalker.h"
#include "nsIDOMXMLDocument.h"
#include "nsIDocShell.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "nsIFrameLoader.h"
#include "nsILoadContext.h"
#include "nsIProtocolHandler.h"
#include "nsITabParent.h"
#include "nsIWebBrowserPersist.h"
#include "nsNetUtil.h"

using mozilla::dom::HTMLInputElement;
using mozilla::dom::HTMLSharedElement;
using mozilla::dom::HTMLSharedObjectElement;

/* static */ nsresult
nsWebBrowserPersistDocument::Create(nsIFrameLoader* aLoader,
                                    nsIWebBrowserPersistDocumentReceiver* aRecv)
{
    nsresult rv;

    nsCOMPtr<nsIDocShell> ds;
    rv = aLoader->GetDocShell(getter_AddRefs(ds));
    NS_ENSURE_SUCCESS(rv, rv);
    if (ds) {
        nsCOMPtr<nsIDocument> doc = do_GetInterface(ds);
        NS_ENSURE_STATE(doc);
        nsCOMPtr<nsIWebBrowserPersistable> pdoc = do_QueryInterface(doc);
        NS_ENSURE_STATE(pdoc);
        return pdoc->StartPersistence(aRecv);
    }
    nsCOMPtr<nsITabParent> tp;
    rv = aLoader->GetTabParent(getter_AddRefs(tp));
    NS_ENSURE_SUCCESS(rv, rv);
    if (tp) {
        auto* tpp = mozilla::dom::TabParent::GetFrom(tp);
        NS_ENSURE_STATE(tpp);
        auto* actor = new nsWebBrowserPersistDocumentParent();
        actor->SetOnReady(aRecv);
        return tpp->SendPWebBrowserPersistDocumentConstructor(actor)
            ? NS_OK : NS_ERROR_FAILURE;
    }

    return NS_ERROR_NO_CONTENT;
}

NS_IMPL_ISUPPORTS(nsWebBrowserPersistDocument, nsIWebBrowserPersistDocument)

nsWebBrowserPersistDocument::nsWebBrowserPersistDocument(nsIDocument* aDocument)
: mDocument(aDocument)
, mPersistFlags(0)
{
    MOZ_ASSERT(mDocument);
}

nsWebBrowserPersistDocument::~nsWebBrowserPersistDocument()
{
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::SetPersistFlags(uint32_t aFlags)
{
    mPersistFlags = aFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::GetPersistFlags(uint32_t* aFlags)
{
    *aFlags = mPersistFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::GetIsPrivate(bool* aIsPrivate)
{
    nsCOMPtr<nsILoadContext> privacyContext = mDocument->GetLoadContext();
    *aIsPrivate = privacyContext && privacyContext->UsePrivateBrowsing();
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::GetDocumentURI(nsACString& aURISpec)
{
    nsCOMPtr<nsIURI> uri = mDocument->GetDocumentURI();
    if (!uri) {
        return NS_ERROR_UNEXPECTED;
    }
    return uri->GetSpec(aURISpec);
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::GetBaseURI(nsACString& aURISpec)
{
    nsCOMPtr<nsIURI> uri = GetBaseURI();
    if (!uri) {
        return NS_ERROR_UNEXPECTED;
    }
    return uri->GetSpec(aURISpec);
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::GetContentType(nsACString& aContentType)
{
    nsAutoString utf16Type;
    nsresult rv;

    rv = mDocument->GetContentType(utf16Type);
    NS_ENSURE_SUCCESS(rv, rv);
    aContentType = NS_ConvertUTF16toUTF8(utf16Type);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocument::GetCharSet(nsACString& aCharSet)
{
    aCharSet = GetCharSet();
    return NS_OK;
}

const nsCString&
nsWebBrowserPersistDocument::GetCharSet() const
{
    return mDocument->GetDocumentCharacterSet();
}


uint32_t
nsWebBrowserPersistDocument::GetPersistFlags() const
{
    return mPersistFlags;
}


already_AddRefed<nsIURI>
nsWebBrowserPersistDocument::GetBaseURI() const
{
    return mDocument->GetBaseURI();
}

// Ordered so that typical documents work fastest.
//                                    strlen("blockquote")==10
static const char kSpecialXHTMLTags[][11] = {
    "body",
    "head",
    "img",
    "script",
    "a",
    "area",
    "link",
    "input",
    "frame",
    "iframe",
    "object",
    "applet",
    "form",
    "blockquote",
    "q",
    "del",
    "ins"
};

static bool IsSpecialXHTMLTag(nsIDOMNode *aNode)
{
    nsAutoString tmp;
    aNode->GetNamespaceURI(tmp);
    if (!tmp.EqualsLiteral("http://www.w3.org/1999/xhtml")) {
        return false;
    }

    aNode->GetLocalName(tmp);
    for (uint32_t i = 0; i < mozilla::ArrayLength(kSpecialXHTMLTags); i++) {
        if (tmp.EqualsASCII(kSpecialXHTMLTags[i])) {
            // XXX This element MAY have URI attributes, but
            //     we are not actually checking if they are present.
            //     That would slow us down further, and I am not so sure
            //     how important that would be.
            return true;
        }
    }

    return false;
}

static bool HasSpecialXHTMLTags(nsIDOMNode *aParent)
{
    if (IsSpecialXHTMLTag(aParent)) {
        return true;
    }

    nsCOMPtr<nsIDOMNodeList> list;
    aParent->GetChildNodes(getter_AddRefs(list));
    if (list) {
        uint32_t count;
        list->GetLength(&count);
        uint32_t i;
        for (i = 0; i < count; i++) {
            nsCOMPtr<nsIDOMNode> node;
            list->Item(i, getter_AddRefs(node));
            if (!node) {
                break;
            }
            uint16_t nodeType;
            node->GetNodeType(&nodeType);
            if (nodeType == nsIDOMNode::ELEMENT_NODE) {
                return HasSpecialXHTMLTags(node);
            }
        }
    }

    return false;
}

static bool NeedXHTMLBaseTag(nsIDOMDocument *aDocument)
{
    nsCOMPtr<nsIDOMElement> docElement;
    aDocument->GetDocumentElement(getter_AddRefs(docElement));

    nsCOMPtr<nsIDOMNode> node(do_QueryInterface(docElement));
    if (node) {
        return HasSpecialXHTMLTags(node);
    }

    return false;
}

// Set document base. This could create an invalid XML document (still well-formed).
NS_IMETHODIMP
nsWebBrowserPersistDocument::ForceBaseElement()
{
    if (mPersistFlags
        & nsIWebBrowserPersist::PERSIST_FLAGS_NO_BASE_TAG_MODIFICATIONS) {
        return NS_OK;
    }

    nsAutoCString uriSpec;
    nsresult rv = GetBaseURI(uriSpec);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(mDocument);
    NS_ENSURE_TRUE(domDoc, NS_ERROR_UNEXPECTED);
    nsCOMPtr<nsIDOMXMLDocument> xmlDoc;
    nsCOMPtr<nsIDOMHTMLDocument> htmlDoc = do_QueryInterface(domDoc);
    if (!htmlDoc) {
        xmlDoc = do_QueryInterface(domDoc);
        if (!xmlDoc) {
            return NS_ERROR_FAILURE;
        }
    }

    NS_NAMED_LITERAL_STRING(kXHTMLNS, "http://www.w3.org/1999/xhtml");
    NS_NAMED_LITERAL_STRING(kHead, "head");

    // Find the head element
    nsCOMPtr<nsIDOMElement> headElement;
    nsCOMPtr<nsIDOMNodeList> headList;
    if (xmlDoc) {
        // First see if there is XHTML content that needs base
        // tags.
        if (!NeedXHTMLBaseTag(domDoc)) {
            return NS_OK;
        }

        domDoc->GetElementsByTagNameNS(
            kXHTMLNS,
            kHead, getter_AddRefs(headList));
    } else {
        domDoc->GetElementsByTagName(
            kHead, getter_AddRefs(headList));
    }
    if (headList) {
        nsCOMPtr<nsIDOMNode> headNode;
        headList->Item(0, getter_AddRefs(headNode));
        headElement = do_QueryInterface(headNode);
    }
    if (!headElement) {
        // Create head and insert as first element
        nsCOMPtr<nsIDOMNode> firstChildNode;
        nsCOMPtr<nsIDOMNode> newNode;
        if (xmlDoc) {
            domDoc->CreateElementNS(
                kXHTMLNS,
                kHead, getter_AddRefs(headElement));
        } else {
            domDoc->CreateElement(
                kHead, getter_AddRefs(headElement));
        }
        nsCOMPtr<nsIDOMElement> documentElement;
        domDoc->GetDocumentElement(getter_AddRefs(documentElement));
        if (documentElement) {
            documentElement->GetFirstChild(getter_AddRefs(firstChildNode));
            documentElement->InsertBefore(headElement, firstChildNode, getter_AddRefs(newNode));
        }
    }
    if (!headElement) {
        return NS_ERROR_FAILURE;
    }

    // Find or create the BASE element
    NS_NAMED_LITERAL_STRING(kBase, "base");
    nsCOMPtr<nsIDOMElement> baseElement;
    nsCOMPtr<nsIDOMHTMLCollection> baseList;
    if (xmlDoc) {
        headElement->GetElementsByTagNameNS(
            kXHTMLNS,
            kBase, getter_AddRefs(baseList));
    } else {
        headElement->GetElementsByTagName(
            kBase, getter_AddRefs(baseList));
    }
    if (baseList) {
        nsCOMPtr<nsIDOMNode> baseNode;
        baseList->Item(0, getter_AddRefs(baseNode));
        baseElement = do_QueryInterface(baseNode);
    }

    // Add the BASE element
    if (!baseElement) {
      nsCOMPtr<nsIDOMNode> newNode;
      if (xmlDoc) {
          domDoc->CreateElementNS(
              kXHTMLNS,
              kBase, getter_AddRefs(baseElement));
      } else {
          domDoc->CreateElement(
              kBase, getter_AddRefs(baseElement));
      }
      headElement->AppendChild(baseElement, getter_AddRefs(newNode));
    }
    if (!baseElement) {
        return NS_ERROR_FAILURE;
    }
    NS_ConvertUTF8toUTF16 href(uriSpec);
    baseElement->SetAttribute(NS_LITERAL_STRING("href"), href);

    return NS_OK;
}


namespace {

class ResourceReader final : public nsIWebBrowserPersistDocumentReceiver {
public:
    ResourceReader(nsWebBrowserPersistDocument* aParent,
                   nsIWebBrowserPersistResourceVisitor* aVisitor);
    nsresult OnWalkDOMNode(nsIDOMNode* aNode);
    void DocumentDone(nsresult aStatus);

    NS_DECL_NSIWEBBROWSERPERSISTDOCUMENTRECEIVER
    NS_DECL_ISUPPORTS

private:
    nsRefPtr<nsWebBrowserPersistDocument> mParent;
    nsCOMPtr<nsIWebBrowserPersistResourceVisitor> mVisitor;
    nsCOMPtr<nsIURI> mCurrentBaseURI;
    uint32_t mPersistFlags;
    size_t mOutstandingDocuments;
    nsresult mEndStatus;

    nsresult OnWalkURI(const nsACString& aURISpec);
    nsresult OnWalkURI(nsIURI* aURI);
    nsresult OnWalkAttribute(nsIDOMNode* aNode,
                             const char* aAttribute,
                             const char* aNamespaceURI = "");
    nsresult OnWalkSubframe(nsIDOMNode* aNode);

    bool IsFlagSet(uint32_t aFlag) const {
        return mParent->GetPersistFlags() & aFlag;
    }

    ~ResourceReader();

    using IWBP = nsIWebBrowserPersist;
};

NS_IMPL_ISUPPORTS(ResourceReader, nsIWebBrowserPersistDocumentReceiver)

ResourceReader::ResourceReader(nsWebBrowserPersistDocument* aParent,
                               nsIWebBrowserPersistResourceVisitor* aVisitor)
: mParent(aParent)
, mVisitor(aVisitor)
, mCurrentBaseURI(aParent->GetBaseURI())
, mPersistFlags(aParent->GetPersistFlags())
, mOutstandingDocuments(1)
, mEndStatus(NS_OK)
{
    MOZ_ASSERT(mCurrentBaseURI);
}

ResourceReader::~ResourceReader()
{
    MOZ_ASSERT(mOutstandingDocuments == 0);
}

void
ResourceReader::DocumentDone(nsresult aStatus)
{
    MOZ_ASSERT(mOutstandingDocuments > 0);
    if (NS_SUCCEEDED(mEndStatus)) {
        mEndStatus = aStatus;
    }
    if (--mOutstandingDocuments == 0) {
        mVisitor->EndVisit(mParent, mEndStatus);
    }
}

nsresult
ResourceReader::OnWalkSubframe(nsIDOMNode* aNode)
{
    ++mOutstandingDocuments;
    nsCOMPtr<nsIFrameLoaderOwner> loaderOwner = do_QueryInterface(aNode);
    NS_ENSURE_STATE(loaderOwner);
    nsCOMPtr<nsIFrameLoader> loader;
    nsresult rv = loaderOwner->GetFrameLoader(getter_AddRefs(loader));
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_STATE(loader);
    rv = nsWebBrowserPersistDocument::Create(loader, this);
    if (NS_FAILED(rv)) {
        // FIXME: should NS_ERROR_NO_CONTENT be ignored?
        DocumentDone(rv);
    }
    return rv;
}

NS_IMETHODIMP
ResourceReader::OnDocumentReady(nsIWebBrowserPersistDocument* aDocument)
{
    if (aDocument) {
        mVisitor->VisitDocument(mParent, aDocument);
        DocumentDone(NS_OK);
    } else {
        DocumentDone(NS_ERROR_FAILURE);
    }
    return NS_OK;
}

nsresult
ResourceReader::OnWalkURI(nsIURI* aURI)
{
    // Test if this URI should be persisted. By default
    // we should assume the URI  is persistable.
    bool doNotPersistURI;
    nsresult rv = NS_URIChainHasFlags(aURI,
                                      nsIProtocolHandler::URI_NON_PERSISTABLE,
                                      &doNotPersistURI);
    if (NS_SUCCEEDED(rv) && doNotPersistURI) {
        return NS_OK;
    }

    nsAutoCString stringURI;
    rv = aURI->GetSpec(stringURI);
    NS_ENSURE_SUCCESS(rv, rv);
    return mVisitor->VisitURI(mParent, stringURI);
}

nsresult
ResourceReader::OnWalkURI(const nsACString& aURISpec)
{
    nsresult rv;
    nsCOMPtr<nsIURI> uri;

    rv = NS_NewURI(getter_AddRefs(uri),
                   aURISpec,
                   mParent->GetCharSet().get(),
                   mCurrentBaseURI);
    NS_ENSURE_SUCCESS(rv, rv);
    return OnWalkURI(uri);
}

static nsresult
ExtractAttribute(nsIDOMNode* aNode,
                 const char* aAttribute,
                 const char* aNamespaceURI,
                 nsCString&  aValue)
{
    nsCOMPtr<nsIDOMElement> element = do_QueryInterface(aNode);
    MOZ_ASSERT(element);

    // Find the named URI attribute on the (element) node and store
    // a reference to the URI that maps onto a local file name

    nsCOMPtr<nsIDOMMozNamedAttrMap> attrMap;
    nsresult rv = element->GetAttributes(getter_AddRefs(attrMap));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    NS_ConvertASCIItoUTF16 namespaceURI(aNamespaceURI);
    NS_ConvertASCIItoUTF16 attribute(aAttribute);
    nsCOMPtr<nsIDOMAttr> attr;
    rv = attrMap->GetNamedItemNS(namespaceURI, attribute, getter_AddRefs(attr));
    NS_ENSURE_SUCCESS(rv, rv);
    if (attr) {
        nsAutoString value;
        rv = attr->GetValue(value);
        NS_ENSURE_SUCCESS(rv, rv);
        aValue = NS_ConvertUTF16toUTF8(value);
    } else {
        aValue.Truncate();
    }
    return NS_OK;
}

nsresult
ResourceReader::OnWalkAttribute(nsIDOMNode* aNode,
                                const char* aAttribute,
                                const char* aNamespaceURI)
{
    nsAutoCString uriSpec;
    nsresult rv = ExtractAttribute(aNode, aAttribute, aNamespaceURI, uriSpec);
    NS_ENSURE_SUCCESS(rv, rv);
    if (uriSpec.IsEmpty()) {
        return NS_OK;
    }
    return OnWalkURI(uriSpec);
}

static nsresult
GetXMLStyleSheetLink(nsIDOMProcessingInstruction *aPI, nsAString &aHref)
{
    nsresult rv;
    nsAutoString data;
    rv = aPI->GetData(data);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    nsContentUtils::GetPseudoAttributeValue(data, nsGkAtoms::href, aHref);
    return NS_OK;
}

nsresult
ResourceReader::OnWalkDOMNode(nsIDOMNode* aNode)
{
    nsresult rv;
    
    // Fixup xml-stylesheet processing instructions
    nsCOMPtr<nsIDOMProcessingInstruction> nodeAsPI = do_QueryInterface(aNode);
    if (nodeAsPI) {
        nsAutoString target;
        rv = nodeAsPI->GetTarget(target);
        NS_ENSURE_SUCCESS(rv, rv);
        if (target.EqualsLiteral("xml-stylesheet")) {
            nsAutoString href;
            GetXMLStyleSheetLink(nodeAsPI, href);
            if (!href.IsEmpty()) {
                return OnWalkURI(NS_ConvertUTF16toUTF8(href));
            }
        }
        return NS_OK;
    }

    nsCOMPtr<nsIContent> content = do_QueryInterface(aNode);
    if (!content) {
        return NS_OK;
    }

    // Test the node to see if it's an image, frame, iframe, css, js
    nsCOMPtr<nsIDOMHTMLImageElement> nodeAsImage = do_QueryInterface(aNode);
    if (nodeAsImage) {
        return OnWalkAttribute(aNode, "src");
    }

    if (content->IsSVGElement(nsGkAtoms::img)) {
        return OnWalkAttribute(aNode, "href", "http://www.w3.org/1999/xlink");
    }

    nsCOMPtr<nsIDOMHTMLMediaElement> nodeAsMedia = do_QueryInterface(aNode);
    if (nodeAsMedia) {
        return OnWalkAttribute(aNode, "src");
    }
    nsCOMPtr<nsIDOMHTMLSourceElement> nodeAsSource = do_QueryInterface(aNode);
    if (nodeAsSource) {
        return OnWalkAttribute(aNode, "src");
    }

    if (content->IsHTMLElement(nsGkAtoms::body)) {
        return OnWalkAttribute(aNode, "background");
    }

    if (content->IsHTMLElement(nsGkAtoms::table)) {
        return OnWalkAttribute(aNode, "background");
    }

    if (content->IsHTMLElement(nsGkAtoms::tr)) {
        return OnWalkAttribute(aNode, "background");
    }

    if (content->IsAnyOfHTMLElements(nsGkAtoms::td, nsGkAtoms::th)) {
        return OnWalkAttribute(aNode, "background");
    }

    nsCOMPtr<nsIDOMHTMLScriptElement> nodeAsScript = do_QueryInterface(aNode);
    if (nodeAsScript) {
        return OnWalkAttribute(aNode, "src");
    }

    if (content->IsSVGElement(nsGkAtoms::script)) {
        return OnWalkAttribute(aNode, "href", "http://www.w3.org/1999/xlink");
    }

    nsCOMPtr<nsIDOMHTMLEmbedElement> nodeAsEmbed = do_QueryInterface(aNode);
    if (nodeAsEmbed) {
        return OnWalkAttribute(aNode, "src");
    }

    nsCOMPtr<nsIDOMHTMLObjectElement> nodeAsObject = do_QueryInterface(aNode);
    if (nodeAsObject) {
        return OnWalkAttribute(aNode, "data");
    }

    nsCOMPtr<nsIDOMHTMLAppletElement> nodeAsApplet = do_QueryInterface(aNode);
    if (nodeAsApplet) {
        // For an applet, relative URIs are resolved relative to the
        // codebase (which is resolved relative to the base URI).
        nsCOMPtr<nsIURI> oldBase = mCurrentBaseURI;
        nsAutoString codebase;
        rv = nodeAsApplet->GetCodeBase(codebase);
        NS_ENSURE_SUCCESS(rv, rv);
        if (!codebase.IsEmpty()) {
            nsCOMPtr<nsIURI> baseURI;
            rv = NS_NewURI(getter_AddRefs(baseURI), codebase,
                           mParent->GetCharSet().get(), mCurrentBaseURI);
            NS_ENSURE_SUCCESS(rv, rv);
            if (baseURI) {
                mCurrentBaseURI = baseURI;
                // Must restore this before returning (or ENSURE'ing).
            }
        }

        // We only store 'code' locally if there is no 'archive',
        // otherwise we assume the archive file(s) contains it (bug 430283).
        nsAutoCString archiveAttr;
        rv = ExtractAttribute(aNode, "archive", "", archiveAttr);
        if (NS_SUCCEEDED(rv)) {
            if (!archiveAttr.IsEmpty()) {
                rv = OnWalkURI(archiveAttr);
            } else {
                rv = OnWalkAttribute(aNode, "core");
            }
        }

        // restore the base URI we really want to have
        // FIXME jld: don't we have a RAII class for this now?
        mCurrentBaseURI = oldBase;
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLLinkElement> nodeAsLink = do_QueryInterface(aNode);
    if (nodeAsLink) {
        // Test if the link has a rel value indicating it to be a stylesheet
        nsAutoString linkRel;
        if (NS_SUCCEEDED(nodeAsLink->GetRel(linkRel)) && !linkRel.IsEmpty()) {
            nsReadingIterator<char16_t> start;
            nsReadingIterator<char16_t> end;
            nsReadingIterator<char16_t> current;

            linkRel.BeginReading(start);
            linkRel.EndReading(end);

            // Walk through space delimited string looking for "stylesheet"
            for (current = start; current != end; ++current) {
                // Ignore whitespace
                if (nsCRT::IsAsciiSpace(*current)) {
                    continue;
                }

                // Grab the next space delimited word
                nsReadingIterator<char16_t> startWord = current;
                do {
                    ++current;
                } while (current != end && !nsCRT::IsAsciiSpace(*current));

                // Store the link for fix up if it says "stylesheet"
                if (Substring(startWord, current)
                        .LowerCaseEqualsLiteral("stylesheet")) {
                    OnWalkAttribute(aNode, "href");
                    return NS_OK;
                }
                if (current == end) {
                    break;
                }
            }
        }
        return NS_OK;
    }

    nsCOMPtr<nsIDOMHTMLFrameElement> nodeAsFrame = do_QueryInterface(aNode);
    if (nodeAsFrame) {
        return OnWalkSubframe(aNode);
    }

    nsCOMPtr<nsIDOMHTMLIFrameElement> nodeAsIFrame = do_QueryInterface(aNode);
    if (nodeAsIFrame && !(mPersistFlags &
                          IWBP::PERSIST_FLAGS_IGNORE_IFRAMES)) {
        return OnWalkSubframe(aNode);
    }

    nsCOMPtr<nsIDOMHTMLInputElement> nodeAsInput = do_QueryInterface(aNode);
    if (nodeAsInput) {
        return OnWalkAttribute(aNode, "src");
    }

    return NS_OK;
}

class PersistNodeFixup final : public nsIDocumentEncoderNodeFixup {
public:
    PersistNodeFixup(nsWebBrowserPersistDocument* aParent,
                     nsIWebBrowserPersistMap* aMap,
                     nsIURI* aTargetURI);

    NS_DECL_ISUPPORTS
    NS_DECL_NSIDOCUMENTENCODERNODEFIXUP
private:
    virtual ~PersistNodeFixup() { }
    nsRefPtr<nsWebBrowserPersistDocument> mParent;
    nsClassHashtable<nsCStringHashKey, nsCString> mMap;
    nsCOMPtr<nsIURI> mCurrentBaseURI;
    nsCOMPtr<nsIURI> mTargetBaseURI;

    bool IsFlagSet(uint32_t aFlag) const {
        return mParent->GetPersistFlags() & aFlag;
    }

    nsresult GetNodeToFixup(nsIDOMNode* aNodeIn, nsIDOMNode** aNodeOut);
    nsresult FixupURI(nsAString& aURI);
    nsresult FixupAttribute(nsIDOMNode* aNode,
                            const char* aAttribute,
                            const char* aNamespaceURI = "");
    nsresult FixupAnchor(nsIDOMNode* aNode);
    nsresult FixupXMLStyleSheetLink(nsIDOMProcessingInstruction* aPI,
                                    const nsAString& aHref);

    using IWBP = nsIWebBrowserPersist;
};

NS_IMPL_ISUPPORTS(PersistNodeFixup, nsIDocumentEncoderNodeFixup)

PersistNodeFixup::PersistNodeFixup(nsWebBrowserPersistDocument* aParent,
                                   nsIWebBrowserPersistMap* aMap,
                                   nsIURI* aTargetURI)
: mParent(aParent)
, mCurrentBaseURI(aParent->GetBaseURI())
, mTargetBaseURI(aTargetURI)
{
    uint32_t mapSize;
    nsresult rv = aMap->GetNumMappedURIs(&mapSize);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    NS_ENSURE_SUCCESS_VOID(rv);
    for (uint32_t i = 0; i < mapSize; ++i) {
        nsAutoCString urlFrom;
        nsCString* urlTo = new nsCString();

        rv = aMap->GetURIMapping(i, urlFrom, *urlTo);
        MOZ_ASSERT(NS_SUCCEEDED(rv));
        if (NS_SUCCEEDED(rv)) {
            mMap.Put(urlFrom, urlTo);
        }
    }
}

nsresult
PersistNodeFixup::GetNodeToFixup(nsIDOMNode *aNodeIn, nsIDOMNode **aNodeOut)
{
    // Avoid mixups in FixupNode that could leak objects; this goes
    // against the usual out parameter convention, but it's a private
    // method so shouldn't be a problem.
    MOZ_ASSERT(!*aNodeOut);

    if (!IsFlagSet(IWBP::PERSIST_FLAGS_FIXUP_ORIGINAL_DOM)) {
        nsresult rv = aNodeIn->CloneNode(false, 1, aNodeOut);
        NS_ENSURE_SUCCESS(rv, rv);
    } else {
        NS_ADDREF(*aNodeOut = aNodeIn);
    }
    nsCOMPtr<nsIDOMHTMLElement> element(do_QueryInterface(*aNodeOut));
    if (element) {
        // Make sure this is not XHTML
        nsAutoString namespaceURI;
        element->GetNamespaceURI(namespaceURI);
        if (namespaceURI.IsEmpty()) {
            // This is a tag-soup node.  It may have a _base_href attribute
            // stuck on it by the parser, but since we're fixing up all URIs
            // relative to the overall document base that will screw us up.
            // Just remove the _base_href.
            element->RemoveAttribute(NS_LITERAL_STRING("_base_href"));
        }
    }
    return NS_OK;
}

nsresult
PersistNodeFixup::FixupURI(nsAString &aURI)
{
    // get the current location of the file (absolutized)
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), aURI,
                            mParent->GetCharSet().get(), mCurrentBaseURI);
    NS_ENSURE_SUCCESS(rv, rv);
    nsAutoCString spec;
    rv = uri->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);

    const nsCString* replacement = mMap.Get(spec);
    if (!replacement) {
        // FIXME: should this be less fatal now that things are async?
        // But see also "Perhaps this link is..." in FixupNode.
        // (Also all the Fixup* callers that drop the rv....)
        return NS_ERROR_FAILURE;
    }
    if (!replacement->IsEmpty()) {
        aURI = NS_ConvertUTF8toUTF16(*replacement);
    }
    return NS_OK;
}

nsresult
PersistNodeFixup::FixupAttribute(nsIDOMNode* aNode,
                                 const char* aAttribute,
                                 const char* aNamespaceURI)
{
    nsCOMPtr<nsIDOMElement> element = do_QueryInterface(aNode);
    MOZ_ASSERT(element);

    nsCOMPtr<nsIDOMMozNamedAttrMap> attrMap;
    nsresult rv = element->GetAttributes(getter_AddRefs(attrMap));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    NS_ConvertASCIItoUTF16 attribute(aAttribute);
    NS_ConvertASCIItoUTF16 namespaceURI(aNamespaceURI);
    nsCOMPtr<nsIDOMAttr> attr;
    rv = attrMap->GetNamedItemNS(namespaceURI, attribute, getter_AddRefs(attr));
    if (attr) {
        nsString uri;
        attr->GetValue(uri);
        rv = FixupURI(uri);
        if (NS_SUCCEEDED(rv)) {
            attr->SetValue(uri);
        }
    }

    return rv;
}

nsresult
PersistNodeFixup::FixupAnchor(nsIDOMNode *aNode)
{
    if (IsFlagSet(IWBP::PERSIST_FLAGS_DONT_FIXUP_LINKS)) {
        return NS_OK;
    }

    nsCOMPtr<nsIDOMElement> element = do_QueryInterface(aNode);
    MOZ_ASSERT(element);

    nsCOMPtr<nsIDOMMozNamedAttrMap> attrMap;
    nsresult rv = element->GetAttributes(getter_AddRefs(attrMap));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    // Make all anchor links absolute so they point off onto the Internet
    nsString attribute(NS_LITERAL_STRING("href"));
    nsCOMPtr<nsIDOMAttr> attr;
    rv = attrMap->GetNamedItem(attribute, getter_AddRefs(attr));
    if (attr) {
        nsString oldValue;
        attr->GetValue(oldValue);
        NS_ConvertUTF16toUTF8 oldCValue(oldValue);

        // Skip empty values and self-referencing bookmarks
        if (oldCValue.IsEmpty() || oldCValue.CharAt(0) == '#') {
            return NS_OK;
        }

        // if saving file to same location, we don't need to do any fixup
        bool isEqual;
        if (mTargetBaseURI &&
            NS_SUCCEEDED(mCurrentBaseURI->Equals(mTargetBaseURI, &isEqual)) &&
            isEqual) {
            return NS_OK;
        }

        nsCOMPtr<nsIURI> relativeURI;
        relativeURI = IsFlagSet(IWBP::PERSIST_FLAGS_FIXUP_LINKS_TO_DESTINATION)
                      ? mTargetBaseURI : mCurrentBaseURI;
        // Make a new URI to replace the current one
        nsCOMPtr<nsIURI> newURI;
        rv = NS_NewURI(getter_AddRefs(newURI), oldCValue,
                       mParent->GetCharSet().get(), relativeURI);
        if (NS_SUCCEEDED(rv) && newURI)
        {
            newURI->SetUserPass(EmptyCString());
            nsAutoCString uriSpec;
            newURI->GetSpec(uriSpec);
            attr->SetValue(NS_ConvertUTF8toUTF16(uriSpec));
        }
    }

    return NS_OK;
}

static void
AppendXMLAttr(const nsAString& key, const nsAString& aValue, nsAString& aBuffer)
{
    if (!aBuffer.IsEmpty()) {
        aBuffer.Append(' ');
    }
    aBuffer.Append(key);
    aBuffer.AppendLiteral("=\"");
    for (size_t i = 0; i < aValue.Length(); ++i) {
        switch (aValue[i]) {
            case '&':
                aBuffer.AppendLiteral("&amp;");
                break;
            case '<':
                aBuffer.AppendLiteral("&lt;");
                break;
            case '>':
                aBuffer.AppendLiteral("&gt;");
                break;
            case '"':
                aBuffer.AppendLiteral("&quot;");
                break;
            default:
                aBuffer.Append(aValue[i]);
                break;
        }
    }
    aBuffer.Append('"');
}

nsresult
PersistNodeFixup::FixupXMLStyleSheetLink(nsIDOMProcessingInstruction* aPI,
                                         const nsAString& aHref)
{
    NS_ENSURE_ARG_POINTER(aPI);
    nsresult rv = NS_OK;

    nsAutoString data;
    rv = aPI->GetData(data);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    nsAutoString href;
    nsContentUtils::GetPseudoAttributeValue(data,
                                            nsGkAtoms::href,
                                            href);

    // Construct and set a new data value for the xml-stylesheet
    if (!aHref.IsEmpty() && !href.IsEmpty())
    {
        nsAutoString alternate;
        nsAutoString charset;
        nsAutoString title;
        nsAutoString type;
        nsAutoString media;

        nsContentUtils::GetPseudoAttributeValue(data,
                                                nsGkAtoms::alternate,
                                                alternate);
        nsContentUtils::GetPseudoAttributeValue(data,
                                                nsGkAtoms::charset,
                                                charset);
        nsContentUtils::GetPseudoAttributeValue(data,
                                                nsGkAtoms::title,
                                                title);
        nsContentUtils::GetPseudoAttributeValue(data,
                                                nsGkAtoms::type,
                                                type);
        nsContentUtils::GetPseudoAttributeValue(data,
                                                nsGkAtoms::media,
                                                media);

        nsAutoString newData;
        AppendXMLAttr(NS_LITERAL_STRING("href"), aHref, newData);
        if (!title.IsEmpty()) {
            AppendXMLAttr(NS_LITERAL_STRING("title"), title, newData);
        }
        if (!media.IsEmpty()) {
            AppendXMLAttr(NS_LITERAL_STRING("media"), media, newData);
        }
        if (!type.IsEmpty()) {
            AppendXMLAttr(NS_LITERAL_STRING("type"), type, newData);
        }
        if (!charset.IsEmpty()) {
            AppendXMLAttr(NS_LITERAL_STRING("charset"), charset, newData);
        }
        if (!alternate.IsEmpty()) {
            AppendXMLAttr(NS_LITERAL_STRING("alternate"), alternate, newData);
        }
        aPI->SetData(newData);
    }

    return rv;
}

NS_IMETHODIMP
PersistNodeFixup::FixupNode(nsIDOMNode *aNodeIn,
                            bool *aSerializeCloneKids,
                            nsIDOMNode **aNodeOut)
{
    *aNodeOut = nullptr;
    *aSerializeCloneKids = false;

    uint16_t type;
    nsresult rv = aNodeIn->GetNodeType(&type);
    NS_ENSURE_SUCCESS(rv, rv);
    if (type != nsIDOMNode::ELEMENT_NODE &&
        type != nsIDOMNode::PROCESSING_INSTRUCTION_NODE) {
        return NS_OK;
    }

    // Fixup xml-stylesheet processing instructions
    nsCOMPtr<nsIDOMProcessingInstruction> nodeAsPI = do_QueryInterface(aNodeIn);
    if (nodeAsPI) {
        nsAutoString target;
        nodeAsPI->GetTarget(target);
        if (target.EqualsLiteral("xml-stylesheet"))
        {
            rv = GetNodeToFixup(aNodeIn, aNodeOut);
            if (NS_SUCCEEDED(rv) && *aNodeOut) {
                nsCOMPtr<nsIDOMProcessingInstruction> outNode =
                    do_QueryInterface(*aNodeOut);
                nsAutoString href;
                GetXMLStyleSheetLink(nodeAsPI, href);
                if (!href.IsEmpty()) {
                    FixupURI(href);
                    FixupXMLStyleSheetLink(outNode, href);
                }
            }
        }
        return NS_OK;
    }

    // BASE elements are replaced by a comment so relative links are not hosed.
    if (!IsFlagSet(IWBP::PERSIST_FLAGS_NO_BASE_TAG_MODIFICATIONS)) {
        nsCOMPtr<nsIDOMHTMLBaseElement> nodeAsBase = do_QueryInterface(aNodeIn);
        if (nodeAsBase) {
            nsCOMPtr<nsIDOMDocument> ownerDocument;
            HTMLSharedElement* base =
                static_cast<HTMLSharedElement*>(nodeAsBase.get());
            base->GetOwnerDocument(getter_AddRefs(ownerDocument));
            if (ownerDocument) {
                nsAutoString href;
                base->GetHref(href); // Doesn't matter if this fails
                nsCOMPtr<nsIDOMComment> comment;
                nsAutoString commentText;
                commentText.AssignLiteral(" base ");
                if (!href.IsEmpty()) {
                    commentText += NS_LITERAL_STRING("href=\"") + href
                                   + NS_LITERAL_STRING("\" ");
                }
                rv = ownerDocument->CreateComment(commentText,
                                                  getter_AddRefs(comment));
                if (comment) {
                    return CallQueryInterface(comment, aNodeOut);
                }
            }
            return NS_OK;
        }
    }

    nsCOMPtr<nsIContent> content = do_QueryInterface(aNodeIn);
    if (!content) {
        return NS_OK;
    }

    // Fix up href and file links in the elements
    nsCOMPtr<nsIDOMHTMLAnchorElement> nodeAsAnchor = do_QueryInterface(aNodeIn);
    if (nodeAsAnchor) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAnchor(*aNodeOut);
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLAreaElement> nodeAsArea = do_QueryInterface(aNodeIn);
    if (nodeAsArea) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAnchor(*aNodeOut);
        }
        return rv;
    }

    if (content->IsHTMLElement(nsGkAtoms::body)) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "background");
        }
        return rv;
    }

    if (content->IsHTMLElement(nsGkAtoms::table)) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "background");
        }
        return rv;
    }

    if (content->IsHTMLElement(nsGkAtoms::tr)) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "background");
        }
        return rv;
    }

    if (content->IsAnyOfHTMLElements(nsGkAtoms::td, nsGkAtoms::th)) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "background");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLImageElement> nodeAsImage = do_QueryInterface(aNodeIn);
    if (nodeAsImage) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            // Disable image loads
            nsCOMPtr<nsIImageLoadingContent> imgCon =
                do_QueryInterface(*aNodeOut);
            if (imgCon) {
                imgCon->SetLoadingEnabled(false);
            }
            FixupAnchor(*aNodeOut);
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLMediaElement> nodeAsMedia = do_QueryInterface(aNodeIn);
    if (nodeAsMedia) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLSourceElement> nodeAsSource = do_QueryInterface(aNodeIn);
    if (nodeAsSource) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

    if (content->IsSVGElement(nsGkAtoms::img)) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            // Disable image loads
            nsCOMPtr<nsIImageLoadingContent> imgCon =
                do_QueryInterface(*aNodeOut);
            if (imgCon)
                imgCon->SetLoadingEnabled(false);

            // FixupAnchor(*aNodeOut);  // XXXjwatt: is this line needed?
            // FIXME(jld): Should this be FixupAnchor with an extra param?
            FixupAttribute(*aNodeOut, "href", "http://www.w3.org/1999/xlink");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLScriptElement> nodeAsScript = do_QueryInterface(aNodeIn);
    if (nodeAsScript) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

    if (content->IsSVGElement(nsGkAtoms::script)) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            // FIXME(jld): see above.
            FixupAttribute(*aNodeOut, "href", "http://www.w3.org/1999/xlink");
        }
        return rv;
    }

        nsCOMPtr<nsIDOMHTMLEmbedElement> nodeAsEmbed = do_QueryInterface(aNodeIn);
    if (nodeAsEmbed) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLObjectElement> nodeAsObject = do_QueryInterface(aNodeIn);
    if (nodeAsObject) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "data");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLAppletElement> nodeAsApplet = do_QueryInterface(aNodeIn);
    if (nodeAsApplet) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            nsCOMPtr<nsIDOMHTMLAppletElement> newApplet =
                do_QueryInterface(*aNodeOut);
            // For an applet, relative URIs are resolved relative to the
            // codebase (which is resolved relative to the base URI).
            nsCOMPtr<nsIURI> oldBase = mCurrentBaseURI;
            nsAutoString codebase;
            nodeAsApplet->GetCodeBase(codebase);
            if (!codebase.IsEmpty()) {
                nsCOMPtr<nsIURI> baseURI;
                NS_NewURI(getter_AddRefs(baseURI), codebase,
                          mParent->GetCharSet().get(), mCurrentBaseURI);
                if (baseURI) {
                    mCurrentBaseURI = baseURI;
                }
            }
            // Unset the codebase too, since we'll correctly relativize the
            // code and archive paths.
            static_cast<HTMLSharedObjectElement*>(newApplet.get())->
              RemoveAttribute(NS_LITERAL_STRING("codebase"));
            FixupAttribute(*aNodeOut, "code");
            FixupAttribute(*aNodeOut, "archive");
            // restore the base URI we really want to have
            mCurrentBaseURI = oldBase;
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLLinkElement> nodeAsLink = do_QueryInterface(aNodeIn);
    if (nodeAsLink) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            // First see if the link represents linked content
            rv = FixupAttribute(*aNodeOut, "href");
            if (NS_FAILED(rv)) {
                // Perhaps this link is actually an anchor to related content
                FixupAnchor(*aNodeOut);
            }
            // TODO if "type" attribute == "text/css"
            //        fixup stylesheet
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLFrameElement> nodeAsFrame = do_QueryInterface(aNodeIn);
    if (nodeAsFrame) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLIFrameElement> nodeAsIFrame = do_QueryInterface(aNodeIn);
    if (nodeAsIFrame) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            FixupAttribute(*aNodeOut, "src");
        }
        return rv;
    }

        nsCOMPtr<nsIDOMHTMLInputElement> nodeAsInput = do_QueryInterface(aNodeIn);
    if (nodeAsInput) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            // Disable image loads
            nsCOMPtr<nsIImageLoadingContent> imgCon =
                do_QueryInterface(*aNodeOut);
            if (imgCon) {
                imgCon->SetLoadingEnabled(false);
            }

            FixupAttribute(*aNodeOut, "src");

            nsAutoString valueStr;
            NS_NAMED_LITERAL_STRING(valueAttr, "value");
            // Update element node attributes with user-entered form state
            nsCOMPtr<nsIContent> content = do_QueryInterface(*aNodeOut);
            nsRefPtr<HTMLInputElement> outElt =
              HTMLInputElement::FromContentOrNull(content);
            nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(*aNodeOut);
            switch (formControl->GetType()) {
                case NS_FORM_INPUT_EMAIL:
                case NS_FORM_INPUT_SEARCH:
                case NS_FORM_INPUT_TEXT:
                case NS_FORM_INPUT_TEL:
                case NS_FORM_INPUT_URL:
                case NS_FORM_INPUT_NUMBER:
                case NS_FORM_INPUT_RANGE:
                case NS_FORM_INPUT_DATE:
                case NS_FORM_INPUT_TIME:
                case NS_FORM_INPUT_COLOR:
                    nodeAsInput->GetValue(valueStr);
                    // Avoid superfluous value="" serialization
                    if (valueStr.IsEmpty())
                      outElt->RemoveAttribute(valueAttr);
                    else
                      outElt->SetAttribute(valueAttr, valueStr);
                    break;
                case NS_FORM_INPUT_CHECKBOX:
                case NS_FORM_INPUT_RADIO:
                    bool checked;
                    nodeAsInput->GetChecked(&checked);
                    outElt->SetDefaultChecked(checked);
                    break;
                default:
                    break;
            }
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLTextAreaElement> nodeAsTextArea = do_QueryInterface(aNodeIn);
    if (nodeAsTextArea) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            // Tell the document encoder to serialize the text child we create below
            *aSerializeCloneKids = true;

            nsAutoString valueStr;
            nodeAsTextArea->GetValue(valueStr);

            (*aNodeOut)->SetTextContent(valueStr);
        }
        return rv;
    }

    nsCOMPtr<nsIDOMHTMLOptionElement> nodeAsOption = do_QueryInterface(aNodeIn);
    if (nodeAsOption) {
        rv = GetNodeToFixup(aNodeIn, aNodeOut);
        if (NS_SUCCEEDED(rv) && *aNodeOut) {
            nsCOMPtr<nsIDOMHTMLOptionElement> outElt = do_QueryInterface(*aNodeOut);
            bool selected;
            nodeAsOption->GetSelected(&selected);
            outElt->SetDefaultSelected(selected);
        }
        return rv;
    }

    return NS_OK;
}

} // unnamed namespace

NS_IMETHODIMP
nsWebBrowserPersistDocument::ReadResources(nsIWebBrowserPersistResourceVisitor* aVisitor)
{
    nsresult rv = NS_OK;
    nsCOMPtr<nsIWebBrowserPersistResourceVisitor> visitor = aVisitor;

    nsCOMPtr<nsIDOMNode> docAsNode = do_QueryInterface(mDocument);
    NS_ENSURE_TRUE(docAsNode, NS_ERROR_FAILURE);

    nsCOMPtr<nsIDOMTreeWalker> walker;
    nsCOMPtr<nsIDOMDocument> oldStyleDoc = do_QueryInterface(mDocument);
    MOZ_ASSERT(oldStyleDoc);
    rv = oldStyleDoc->CreateTreeWalker(docAsNode,
            nsIDOMNodeFilter::SHOW_ELEMENT |
                nsIDOMNodeFilter::SHOW_DOCUMENT |
                nsIDOMNodeFilter::SHOW_PROCESSING_INSTRUCTION,
            nullptr, 1, getter_AddRefs(walker));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
    MOZ_ASSERT(walker);

    nsRefPtr<ResourceReader> reader = new ResourceReader(this, aVisitor);
    nsCOMPtr<nsIDOMNode> currentNode;
    walker->GetCurrentNode(getter_AddRefs(currentNode));
    while (currentNode) {
        rv = reader->OnWalkDOMNode(currentNode);
        if (NS_WARN_IF(NS_FAILED(rv))) {
            break;
        }
        rv = walker->NextNode(getter_AddRefs(currentNode));
        if (NS_WARN_IF(NS_FAILED(rv))) {
            break;
        }
    }
    reader->DocumentDone(rv);
    return rv;
}

static uint32_t
ConvertEncoderFlags(uint32_t aEncoderFlags)
{
    uint32_t encoderFlags = 0;

    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_SELECTION_ONLY)
        encoderFlags |= nsIDocumentEncoder::OutputSelectionOnly;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_FORMATTED)
        encoderFlags |= nsIDocumentEncoder::OutputFormatted;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_RAW)
        encoderFlags |= nsIDocumentEncoder::OutputRaw;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_BODY_ONLY)
        encoderFlags |= nsIDocumentEncoder::OutputBodyOnly;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_PREFORMATTED)
        encoderFlags |= nsIDocumentEncoder::OutputPreformatted;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_WRAP)
        encoderFlags |= nsIDocumentEncoder::OutputWrap;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_FORMAT_FLOWED)
        encoderFlags |= nsIDocumentEncoder::OutputFormatFlowed;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ABSOLUTE_LINKS)
        encoderFlags |= nsIDocumentEncoder::OutputAbsoluteLinks;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_BASIC_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeBasicEntities;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_LATIN1_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeLatin1Entities;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_HTML_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeHTMLEntities;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_W3C_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeW3CEntities;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_CR_LINEBREAKS)
        encoderFlags |= nsIDocumentEncoder::OutputCRLineBreak;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_LF_LINEBREAKS)
        encoderFlags |= nsIDocumentEncoder::OutputLFLineBreak;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_NOSCRIPT_CONTENT)
        encoderFlags |= nsIDocumentEncoder::OutputNoScriptContent;
    if (aEncoderFlags & nsIWebBrowserPersist::ENCODE_FLAGS_NOFRAMES_CONTENT)
        encoderFlags |= nsIDocumentEncoder::OutputNoFramesContent;

    return encoderFlags;
}

static bool
ContentTypeEncoderExists(const nsACString& aType)
{
    nsAutoCString contractID(NS_DOC_ENCODER_CONTRACTID_BASE);
    contractID.Append(aType);

    nsCOMPtr<nsIComponentRegistrar> registrar;
    nsresult rv = NS_GetComponentRegistrar(getter_AddRefs(registrar));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    if (NS_SUCCEEDED(rv) && registrar) {
        bool result;
        rv = registrar->IsContractIDRegistered(contractID.get(), &result);
        MOZ_ASSERT(NS_SUCCEEDED(rv));
        return NS_SUCCEEDED(rv) && result;
    }
    return false;
}

void
nsWebBrowserPersistDocument::DecideContentType(nsACString& aContentType)
{
    if (aContentType.IsEmpty()) {
        if (NS_WARN_IF(NS_FAILED(GetContentType(aContentType)))) {
            aContentType.Truncate();
        }
    }
    if (!aContentType.IsEmpty() &&
        !ContentTypeEncoderExists(aContentType)) {
        aContentType.Truncate();
    }
    if (aContentType.IsEmpty()) {
        aContentType.AssignLiteral("text/html");
    }
}

nsresult
nsWebBrowserPersistDocument::GetDocEncoder(const nsACString& aContentType,
                                           uint32_t aEncoderFlags,
                                           nsIDocumentEncoder** aEncoder)
{
    nsresult rv;
    nsAutoCString contractID(NS_DOC_ENCODER_CONTRACTID_BASE);
    contractID.Append(aContentType);
    nsCOMPtr<nsIDocumentEncoder> encoder =
        do_CreateInstance(contractID.get(), &rv);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    rv = encoder->NativeInit(mDocument,
                             NS_ConvertASCIItoUTF16(aContentType),
                             ConvertEncoderFlags(aEncoderFlags));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    nsAutoCString charSet;
    rv = GetCharSet(charSet);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
    rv = encoder->SetCharset(charSet);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    encoder.forget(aEncoder);
    return NS_OK;
}


NS_IMETHODIMP
nsWebBrowserPersistDocument::WriteContent(
    nsIOutputStream* aStream,
    nsIWebBrowserPersistMap* aMap,
    const nsACString& aRequestedContentType,
    uint32_t aEncoderFlags,
    uint32_t aWrapColumn,
    nsIWebBrowserPersistWriteCompletion* aCompletion)
{
    NS_ENSURE_ARG_POINTER(aStream);
    NS_ENSURE_ARG_POINTER(aCompletion);
    nsAutoCString contentType(aRequestedContentType);
    DecideContentType(contentType);

    nsCOMPtr<nsIDocumentEncoder> encoder;
    nsresult rv = GetDocEncoder(contentType, aEncoderFlags,
                                getter_AddRefs(encoder));
    NS_ENSURE_SUCCESS(rv, rv);

    if (aWrapColumn != 0 && (aEncoderFlags
                             & nsIWebBrowserPersist::ENCODE_FLAGS_WRAP)) {
        encoder->SetWrapColumn(aWrapColumn);
    }

    if (aMap) {
        nsCOMPtr<nsIURI> targetURI;
        nsAutoCString targetURISpec;
        rv = aMap->GetTargetBaseURI(targetURISpec);
        if (NS_SUCCEEDED(rv) && !targetURISpec.IsEmpty()) {
            rv = NS_NewURI(getter_AddRefs(targetURI), targetURISpec,
                           /* charset: */ nullptr, /* base: */ nullptr);
            NS_ENSURE_SUCCESS(rv, NS_ERROR_UNEXPECTED);
        } else if (mPersistFlags & nsIWebBrowserPersist::PERSIST_FLAGS_FIXUP_LINKS_TO_DESTINATION) {
            return NS_ERROR_UNEXPECTED;
        }

        rv = encoder->SetNodeFixup(new PersistNodeFixup(this, aMap, targetURI));
        NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
    }

    rv = encoder->EncodeToStream(aStream);
    aCompletion->OnFinish(this, aStream, contentType, rv);
    return NS_OK;
}
