/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistLocalDocument.h"

#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsContentCID.h"
#include "nsIComponentRegistrar.h"
#include "nsIContent.h"
#include "nsIDOMAttr.h"
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
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "nsILoadContext.h"
#include "nsIProtocolHandler.h"
#include "nsIWebBrowserPersist.h"
#include "nsNetUtil.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistLocalDocument, nsIWebBrowserPersistDocument)

nsWebBrowserPersistLocalDocument::nsWebBrowserPersistLocalDocument(nsIDocument* aDocument)
: mDocument(aDocument)
, mPersistFlags(0)
{
    MOZ_ASSERT(mDocument);
}

nsWebBrowserPersistLocalDocument::~nsWebBrowserPersistLocalDocument()
{
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::SetPersistFlags(uint32_t aFlags)
{
    mPersistFlags = aFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetPersistFlags(uint32_t* aFlags)
{
    *aFlags = mPersistFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetIsPrivate(bool* aIsPrivate)
{
    nsCOMPtr<nsILoadContext> privacyContext = mDocument->GetLoadContext();
    *aIsPrivate = privacyContext && privacyContext->UsePrivateBrowsing();
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetDocumentURI(nsACString& aURISpec)
{
    nsCOMPtr<nsIURI> uri = mDocument->GetDocumentURI();
    if (!uri) {
        return NS_ERROR_UNEXPECTED;
    }
    return uri->GetSpec(aURISpec);
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetBaseURI(nsACString& aURISpec)
{
    nsCOMPtr<nsIURI> uri = mDocument->GetBaseURI();
    if (!uri) {
        return NS_ERROR_UNEXPECTED;
    }
    return uri->GetSpec(aURISpec);
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetContentType(nsACString& aContentType)
{
    nsAutoString utf16Type;
    nsresult rv;

    rv = mDocument->GetContentType(utf16Type);
    NS_ENSURE_SUCCESS(rv, rv);
    aContentType = NS_ConvertUTF16toUTF8(utf16Type);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetCharSet(nsACString& aCharSet)
{
    aCharSet = mDocument->GetDocumentCharacterSet();
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::ReadResources(nsIWebBrowserPersistResourceVisitor* aVisitor)
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

    MOZ_ASSERT(!mVisitor);
    NS_ENSURE_FALSE(mVisitor, NS_ERROR_UNEXPECTED);
    mCurrentBaseURI = mDocument->GetBaseURI();
    NS_ENSURE_TRUE(mCurrentBaseURI, NS_ERROR_UNEXPECTED);
    mVisitor.swap(visitor);
    // Don't early return or ENSURE after this point.

    nsCOMPtr<nsIDOMNode> currentNode;
    walker->GetCurrentNode(getter_AddRefs(currentNode));
    while (currentNode) {
        rv = OnWalkDOMNode(currentNode);
        if (NS_WARN_IF(NS_FAILED(rv))) {
            break;
        }
        rv = walker->NextNode(getter_AddRefs(currentNode));
        if (NS_WARN_IF(NS_FAILED(rv))) {
            break;
        }
    }

    mCurrentBaseURI = nullptr;
    visitor.swap(mVisitor);
    visitor->EndVisit(this, rv);
    return rv;
}

nsresult
nsWebBrowserPersistLocalDocument::OnWalkURI(nsIURI* aURI)
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
    return mVisitor->VisitURI(this, stringURI);
}

nsresult
nsWebBrowserPersistLocalDocument::OnWalkURI(const nsACString& aURISpec)
{
    nsresult rv;
    nsCOMPtr<nsIURI> uri;

    rv = NS_NewURI(getter_AddRefs(uri),
                   aURISpec,
                   /* charset: */ nullptr,
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
nsWebBrowserPersistLocalDocument::OnWalkAttribute(nsIDOMNode* aNode,
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

nsresult
nsWebBrowserPersistLocalDocument::OnWalkSubframe(nsIDOMNode*     aNode,
                                                 nsIDOMDocument* aMaybeContent)
{
    if (!aMaybeContent) {
        return NS_OK;
    }
    nsCOMPtr<nsIDocument> contentDoc = do_QueryInterface(aMaybeContent);
    NS_ENSURE_STATE(contentDoc);
    nsRefPtr<nsWebBrowserPersistLocalDocument> subPersist =
        new nsWebBrowserPersistLocalDocument(contentDoc);
    return mVisitor->VisitDocument(this, subPersist);
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
nsWebBrowserPersistLocalDocument::OnWalkDOMNode(nsIDOMNode* aNode)
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
                           /* charset: */ nullptr, mCurrentBaseURI);
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
        nsCOMPtr<nsIDOMDocument> content;
        rv = nodeAsFrame->GetContentDocument(getter_AddRefs(content));
        NS_ENSURE_SUCCESS(rv, rv);
        return OnWalkSubframe(aNode, content);
    }

    nsCOMPtr<nsIDOMHTMLIFrameElement> nodeAsIFrame = do_QueryInterface(aNode);
    if (nodeAsIFrame && !(mPersistFlags &
                          nsIWebBrowserPersist::PERSIST_FLAGS_IGNORE_IFRAMES)) {
        nsCOMPtr<nsIDOMDocument> content;
        rv = nodeAsIFrame->GetContentDocument(getter_AddRefs(content));
        NS_ENSURE_SUCCESS(rv, rv);
        return OnWalkSubframe(aNode, content);
    }

    nsCOMPtr<nsIDOMHTMLInputElement> nodeAsInput = do_QueryInterface(aNode);
    if (nodeAsInput) {
        return OnWalkAttribute(aNode, "src");
    }

    return NS_OK;
}

static uint32_t
PersistFlagsToEncoderFlags(uint32_t aPersistFlags)
{
    uint32_t encoderFlags = 0;

    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_SELECTION_ONLY)
        encoderFlags |= nsIDocumentEncoder::OutputSelectionOnly;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_FORMATTED)
        encoderFlags |= nsIDocumentEncoder::OutputFormatted;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_RAW)
        encoderFlags |= nsIDocumentEncoder::OutputRaw;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_BODY_ONLY)
        encoderFlags |= nsIDocumentEncoder::OutputBodyOnly;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_PREFORMATTED)
        encoderFlags |= nsIDocumentEncoder::OutputPreformatted;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_WRAP)
        encoderFlags |= nsIDocumentEncoder::OutputWrap;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_FORMAT_FLOWED)
        encoderFlags |= nsIDocumentEncoder::OutputFormatFlowed;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ABSOLUTE_LINKS)
        encoderFlags |= nsIDocumentEncoder::OutputAbsoluteLinks;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_BASIC_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeBasicEntities;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_LATIN1_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeLatin1Entities;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_HTML_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeHTMLEntities;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_ENCODE_W3C_ENTITIES)
        encoderFlags |= nsIDocumentEncoder::OutputEncodeW3CEntities;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_CR_LINEBREAKS)
        encoderFlags |= nsIDocumentEncoder::OutputCRLineBreak;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_LF_LINEBREAKS)
        encoderFlags |= nsIDocumentEncoder::OutputLFLineBreak;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_NOSCRIPT_CONTENT)
        encoderFlags |= nsIDocumentEncoder::OutputNoScriptContent;
    if (aPersistFlags & nsIWebBrowserPersist::ENCODE_FLAGS_NOFRAMES_CONTENT)
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

#ifdef notyet
namespace {

class PersistNodeFixup final : nsIDocumentEncoderNodeFixup {
    virtual ~PersistNodeFixup() { }
    // STUFF GOES HERE
public:
    explicit PersistNodeFixup(nsIWebBrowserPersistMap* aMap);

    NS_DECL_ISUPPORTS
    NS_DECL_NSIDOCUMENTENCODERNODEFIXUP
};

} // unnamed namespace
#endif // notyet

void
nsWebBrowserPersistLocalDocument::DecideContentType(nsACString& aContentType)
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
nsWebBrowserPersistLocalDocument::GetDocEncoder(const nsACString& aContentType,
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
                             PersistFlagsToEncoderFlags(mPersistFlags));
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
nsWebBrowserPersistLocalDocument::WriteContent(
    nsIOutputStream* aStream,
    nsIWebBrowserPersistMap* aMap,
    const nsACString& aRequestedContentType,
    uint32_t aWrapColumn,
    nsIWebBrowserPersistWriteCompletion* aCompletion)
{
    nsAutoCString contentType(aRequestedContentType);
    DecideContentType(contentType);

    nsCOMPtr<nsIDocumentEncoder> encoder;
    nsresult rv = GetDocEncoder(contentType, getter_AddRefs(encoder));
    NS_ENSURE_SUCCESS(rv, rv);

    if (aWrapColumn != 0 && (mPersistFlags &
                             nsIWebBrowserPersist::ENCODE_FLAGS_WRAP))
        encoder->SetWrapColumn(aWrapColumn);

#ifdef notyet
    rv = encoder->SetNodeFixup(new PersistNodeFixup(aMap));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
#endif

    rv = encoder->EncodeToStream(aStream);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    aCompletion->OnFinish(this, aStream, contentType);
    return NS_OK;
}
