
#include "config.h"
#include "base/rand_util.h"
#include "content/web_impl_win/BlinkPlatformImpl.h"
#include "content/web_impl_win/WebThreadImpl.h"
#include "content/web_impl_win/WebURLLoaderImpl.h"
#include "content/web_impl_win/WebURLLoaderImplCurl.h"
#include "content/web_impl_win/CurrentTimeImpl.h"
#include "content/web_impl_win/WebThemeEngineImpl.h"
#include "content/web_impl_win/WebMimeRegistryImpl.h"
#include "content/web_impl_win/WebBlobRegistryImpl.h"
#include "content/resources/MissingImageData.h"
#include "content/resources/TextAreaResizeCornerData.h"
#include "content/resources/LocalizedString.h"
#include "content/resources/WebKitWebRes.h"
#include "cc/blink/WebCompositorSupportImpl.h"
#include "cc/raster/RasterTaskWorkerThreadPool.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/Source/core/fetch/MemoryCache.h"
#include "third_party/WebKit/Source/web/WebStorageNamespaceImpl.h"
#include "third_party/WebKit/public/platform/WebScrollbarBehavior.h"
#include "third_party/WebKit/Source/platform/PartitionAllocMemoryDumpProvider.h"
#include "third_party/WebKit/Source/platform/heap/BlinkGCMemoryDumpProvider.h"
#include "third_party/WebKit/Source/bindings/core/v8/V8GCController.h"
#include "third_party/skia/include/core/SkGraphics.h"
#include "net/ActivatingLoaderCheck.h"

#include "gen/blink/core/UserAgentStyleSheets.h"

#include "third_party/WebKit/Source/core/loader/ImageLoader.h" // TODO
#include "third_party/WebKit/Source/core/html/HTMLLinkElement.h" // TODO
#include "third_party/WebKit/Source/core/html/HTMLStyleElement.h" // TODO
#include "third_party/WebKit/Source/core/css/resolver/StyleResolver.h"

#ifdef _DEBUG
extern size_t g_v8MemSize;
extern size_t g_blinkMemSize;
extern size_t g_skiaMemSize;

class CallAddrsRecord;
CallAddrsRecord* g_callAddrsRecord = nullptr;

extern std::set<void*>* g_activatingStyleFetchedImage;
extern std::set<void*>* g_activatingIncrementLoadEventDelayCount;
#endif

namespace blink {
#ifdef _DEBUG
extern std::set<void*>* g_activatingImageLoader;
extern std::set<void*>* g_activatingFontFallbackList;
extern int gStyleFetchedImageCreate;
extern int gStyleFetchedImageNotifyFinished;
#endif
}

namespace content {

class DOMStorageMapWrap {
public:
    DOMStorageMapWrap()
    {
    }
    ~DOMStorageMapWrap()
    {
    }
    blink::DOMStorageMap map;
};

DWORD sCurrentThreadTlsKey = -1;

static WebThreadImpl* currentTlsThread()
{
    //AtomicallyInitializedStaticReference(WTF::ThreadSpecific<WebThreadImpl>, sCurrentThread, new ThreadSpecific<WebThreadImpl>);
    //return sCurrentThread;
    if (-1 != sCurrentThreadTlsKey)
        return (WebThreadImpl*)TlsGetValue(sCurrentThreadTlsKey);

    return nullptr;
}

BlinkPlatformImpl::BlinkPlatformImpl() 
{
    m_mainThreadId = -1;
    m_webThemeEngine = nullptr;
    m_mimeRegistry = nullptr;
    m_webCompositorSupport = nullptr;
    m_webScrollbarBehavior = nullptr;
    m_localStorageStorageMap = nullptr;
    m_sessionStorageStorageMap = nullptr;
    m_storageNamespaceIdCount = 1;
    m_lock = new CRITICAL_SECTION();
    m_threadNum = 0;
	m_ioThread = nullptr;
    m_firstMonotonicallyIncreasingTime = currentTimeImpl(); // (GetTickCount() / 1000.0);
    for (int i = 0; i < m_maxThreadNum; ++i) { m_threads[i] = nullptr; }
    ::InitializeCriticalSection(m_lock);
}

BlinkPlatformImpl::~BlinkPlatformImpl()
{
    ::DeleteCriticalSection(m_lock);
    delete m_lock;
    m_lock = nullptr;
}

void BlinkPlatformImpl::destroyWebInfo()
{
    if (m_webThemeEngine)
        delete m_webThemeEngine;
    m_webThemeEngine = nullptr;

    if (m_mimeRegistry)
        delete m_mimeRegistry;
    m_mimeRegistry = nullptr;

    if (m_webCompositorSupport)
        delete m_webCompositorSupport;
    m_webCompositorSupport = nullptr;

    if (m_webScrollbarBehavior)
        delete m_webScrollbarBehavior;
    m_webScrollbarBehavior = nullptr;

    if (m_localStorageStorageMap)
        delete m_localStorageStorageMap;
    m_localStorageStorageMap = nullptr;

    if (m_sessionStorageStorageMap)
        delete m_sessionStorageStorageMap;
    m_sessionStorageStorageMap = nullptr;
}

void BlinkPlatformImpl::registerMemoryDumpProvider(blink::WebMemoryDumpProvider*) {}
void BlinkPlatformImpl::unregisterMemoryDumpProvider(blink::WebMemoryDumpProvider* provider)
{
    if (provider == blink::BlinkGCMemoryDumpProvider::instance()) {
        
    }

    if (provider == blink::PartitionAllocMemoryDumpProvider::instance()) {
        
    }
}

void BlinkPlatformImpl::preShutdown()
{
    destroyWebInfo();

    if (m_ioThread)
        delete m_ioThread;
    m_ioThread = nullptr;

    WebThread* mainThread = m_mainThread;
    delete mainThread;
}

void BlinkPlatformImpl::shutdown()
{
    blink::memoryCache()->evictResources();
    v8::Isolate::GetCurrent()->LowMemoryNotification();

    cc::RasterTaskWorkerThreadPool* rasterPool = cc::RasterTaskWorkerThreadPool::shared();
    rasterPool->shutdown();

    SkGraphics::PurgeResourceCache();
    SkGraphics::PurgeFontCache();
    SkGraphics::Term();

    net::ActivatingLoaderCheck::inst()->shutdown();

    MemoryCache* memoryCache = MemoryCache::create();
    replaceMemoryCacheForTesting(memoryCache);

    blink::Heap::collectAllGarbage();

    blink::ImageLoader::dispatchPendingLoadEvents();
    blink::ImageLoader::dispatchPendingErrorEvents();
    blink::HTMLLinkElement::dispatchPendingLoadEvents();
    blink::HTMLStyleElement::dispatchPendingLoadEvents();

    ((WebThreadImpl*)m_mainThread)->shutdown();

    if (blink::StyleResolver::styleNotYetAvailable())
        blink::StyleResolver::styleNotYetAvailable()->font().update(nullptr);

    blink::Heap::collectAllGarbage();

#ifdef _DEBUG
//     for (blink::ThreadState* state : blink::ThreadState::attachedThreads())
//         state->snapshot();
#endif

#ifdef _DEBUG
    if (blink::g_activatingImageLoader && !blink::g_activatingImageLoader->empty() ||
        // blink::g_activatingFontFallbackList && !blink::g_activatingFontFallbackList->empty() ||
        g_activatingStyleFetchedImage && !g_activatingStyleFetchedImage->empty() ||
        g_activatingIncrementLoadEventDelayCount && !g_activatingIncrementLoadEventDelayCount->empty())
        DebugBreak();
#endif

    preShutdown();
    blink::Heap::collectAllGarbage();

    blink::shutdown();
    closeThread();

    net::ActivatingLoaderCheck::inst()->destroy();

#ifdef _DEBUG
    size_t v8MemSize = g_v8MemSize;
    v8MemSize += g_blinkMemSize;
    v8MemSize += g_skiaMemSize;
    g_callAddrsRecord;
#endif
    delete this;
}

void BlinkPlatformImpl::doGarbageCollected()
{
    //net::gActivatingLoaderCheck->doGarbageCollected(false);
}

void BlinkPlatformImpl::startGarbageCollectedThread()
{
    mainThread()->postDelayedTask(FROM_HERE, WTF::bind(&BlinkPlatformImpl::doGarbageCollected, this), 3000);
}

void BlinkPlatformImpl::closeThread()
{   
    if (0 != m_threadNum)
        DebugBreak();

    for (int i = 0; i < m_maxThreadNum; ++i) {
        if (nullptr != m_threads[i])
            DebugBreak();
    }
}

blink::WebThread* BlinkPlatformImpl::createThread(const char* name)
{
    if (0 != strcmp(name, "MainThread")) {
        RELEASE_ASSERT(-1 != sCurrentThreadTlsKey);
    }
    WebThreadImpl* threadImpl = new WebThreadImpl(name);

    ::EnterCriticalSection(m_lock);
    m_threads[m_threadNum] = (threadImpl);
    ++m_threadNum;
    if (m_threadNum > m_maxThreadNum)
        DebugBreak();
    ::LeaveCriticalSection(m_lock);

    return threadImpl;
}

void BlinkPlatformImpl::onThreadExit(WebThreadImpl* threadImpl)
{
    ::EnterCriticalSection(m_lock);
    bool find = false;
    for (int i = 0; i < m_maxThreadNum; ++i) {
        if (m_threads[i] != threadImpl)
            continue;
        m_threads[i] = nullptr;
        --m_threadNum;
        find = true;
        break;
    }
    if (!find || m_threadNum < 0)
        DebugBreak();
    ::LeaveCriticalSection(m_lock);
}

void BlinkPlatformImpl::onCurrentThreadWhenWebThreadImplCreated(blink::WebThread* thread)
{
    RELEASE_ASSERT(-1 != sCurrentThreadTlsKey);
    TlsSetValue(sCurrentThreadTlsKey, thread);
}

blink::WebThread* BlinkPlatformImpl::currentThread()
{
    if (-1 == m_mainThreadId) {
        m_mainThreadId = WTF::currentThread();
        m_mainThread = createThread("MainThread");
        sCurrentThreadTlsKey = TlsAlloc();
        onCurrentThreadWhenWebThreadImplCreated(m_mainThread);
        return m_mainThread;
    }
    
    if (WTF::isMainThread())
        return m_mainThread;

    return currentTlsThread();
}

blink::WebThread* BlinkPlatformImpl::tryGetIoThread() const
{
	return m_ioThread;
}

blink::WebThread* BlinkPlatformImpl::ioThread()
{
	if (!m_ioThread)
		m_ioThread = createThread("ioThread");
	return m_ioThread;
}

void BlinkPlatformImpl::cryptographicallyRandomValues(unsigned char* buffer, size_t length)
{
    base::RandBytes(buffer, length);
}

blink::WebURLLoader* BlinkPlatformImpl::createURLLoader()
{
    return new content::WebURLLoaderImpl();
    //return new content::WebURLLoaderImplCurl();
}

const unsigned char* BlinkPlatformImpl::getTraceCategoryEnabledFlag(const char* categoryName)
{
    static const char* dummyCategoryEnabledFlag = "*";
    return reinterpret_cast<const unsigned char*>(dummyCategoryEnabledFlag);
}

blink::WebString BlinkPlatformImpl::defaultLocale()
{
    return blink::WebString::fromUTF8("zh-CN");
}

double BlinkPlatformImpl::currentTime()
{
    return currentTimeImpl();
}

double BlinkPlatformImpl::monotonicallyIncreasingTime() 
{
//     LARGE_INTEGER qpc;
//     QueryPerformanceCounter(&qpc);
//     return qpc.QuadPart / (1000 * 1000);

//     double timeInDouble = (double)GetTickCount();  
//     return (timeInDouble / 1000.0) - m_firstMonotonicallyIncreasingTime;

    double timeInDouble = (double)currentTimeImpl();
    return timeInDouble - m_firstMonotonicallyIncreasingTime;
}

double BlinkPlatformImpl::systemTraceTime() 
{
    DebugBreak();
    return 0; 
}

blink::WebString BlinkPlatformImpl::userAgent()
{
    return blink::WebString("Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/69.0.2171.99 Safari/537.36"); // PC
    //return blink::WebString("Mozilla/5.0 (Linux; Android 4.4.4; en-us; Nexus 4 Build/JOP40D) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/42.0.2307.2 Mobile Safari/537.36");
}

blink::WebData BlinkPlatformImpl::loadResource(const char* name)
{
    if (0 == strcmp("html.css", name))
        return blink::WebData(blink::htmlUserAgentStyleSheet, sizeof(blink::htmlUserAgentStyleSheet));
    else if (0 == strcmp("quirks.css", name))
        return blink::WebData(blink::quirksUserAgentStyleSheet, sizeof(blink::quirksUserAgentStyleSheet));
    else if (0 == strcmp("themeWin.css", name))
        return blink::WebData(blink::themeWinUserAgentStyleSheet, sizeof(blink::themeWinUserAgentStyleSheet));
    else if (0 == strcmp("svg.css", name))
        return blink::WebData(blink::svgUserAgentStyleSheet, sizeof(blink::svgUserAgentStyleSheet));
    else if (0 == strcmp("themeChromiumSkia.css", name))
        return blink::WebData(blink::themeChromiumSkiaUserAgentStyleSheet, sizeof(blink::themeChromiumSkiaUserAgentStyleSheet));
    else if (0 == strcmp("themeChromium.css", name))
        return blink::WebData(blink::themeChromiumUserAgentStyleSheet, sizeof(blink::themeChromiumUserAgentStyleSheet));
    else if (0 == strcmp("themeWinQuirks.css", name))
        return blink::WebData(blink::themeWinQuirksUserAgentStyleSheet, sizeof(blink::themeWinQuirksUserAgentStyleSheet));
    else if (0 == strcmp("missingImage", name))
        return blink::WebData((const char*)content::gMissingImageData, sizeof(content::gMissingImageData));
    else if (0 == strcmp("textAreaResizeCorner", name))
        return blink::WebData((const char*)content::gTextAreaResizeCornerData, sizeof(content::gTextAreaResizeCornerData));
    else if (0 == strcmp("textAreaResizeCorner@2x", name))
        return blink::WebData((const char*)content::gTextAreaResizeCornerData, sizeof(content::gTextAreaResizeCornerData));
    else if (0 == strcmp("mediaControls.css", name))
        return blink::WebData((const char*)blink::mediaControlsAndroidUserAgentStyleSheet, sizeof(blink::mediaControlsAndroidUserAgentStyleSheet));
    //////////////////////////////////////////////////////////////////////////
    else if (0 == strcmp("calendarPicker.css", name))
        return blink::WebData((const char*)content::calendarPickerCss, sizeof(content::calendarPickerCss));
    else if (0 == strcmp("calendarPicker.js", name))
        return blink::WebData((const char*)content::calendarPickerJs, sizeof(content::calendarPickerJs));
    else if (0 == strcmp("colorSuggestionPicker.css", name))
        return blink::WebData((const char*)content::colorSuggestionPickerCss, sizeof(content::colorSuggestionPickerCss));
    else if (0 == strcmp("colorSuggestionPicker.js", name))
        return blink::WebData((const char*)content::colorSuggestionPickerJs, sizeof(content::colorSuggestionPickerJs));
    else if (0 == strcmp("listPicker.css", name))
        return blink::WebData((const char*)content::listPickerCss, sizeof(content::listPickerCss));
    else if (0 == strcmp("listPicker.js", name))
        return blink::WebData((const char*)content::listPickerJs, sizeof(content::listPickerJs));
    else if (0 == strcmp("pickerButton.css", name))
        return blink::WebData((const char*)content::pickerButtonCss, sizeof(content::pickerButtonCss));
    else if (0 == strcmp("pickerCommon.css", name))
        return blink::WebData((const char*)content::pickerCommonCss, sizeof(content::pickerCommonCss));
    else if (0 == strcmp("pickerCommon.js", name))
        return blink::WebData((const char*)content::pickerCommonJs, sizeof(content::pickerCommonJs));
    else if (0 == strcmp("suggestionPicker.css", name))
        return blink::WebData((const char*)content::suggestionPickerCss, sizeof(content::suggestionPickerCss));
    else if (0 == strcmp("suggestionPicker.js", name))
        return blink::WebData((const char*)content::suggestionPickerJs, sizeof(content::suggestionPickerJs));
    
    notImplemented();
    return blink::WebData();
}

blink::WebThemeEngine* BlinkPlatformImpl::themeEngine()
{
    if (nullptr == m_webThemeEngine)
        m_webThemeEngine = new WebThemeEngineImpl();
    return m_webThemeEngine;
}

blink::WebMimeRegistry* BlinkPlatformImpl::mimeRegistry()
{
    if (nullptr == m_mimeRegistry)
        m_mimeRegistry = new WebMimeRegistryImpl();
    return m_mimeRegistry;
}

blink::WebCompositorSupport* BlinkPlatformImpl::compositorSupport()
{
    if (!m_webCompositorSupport)
        m_webCompositorSupport = new cc_blink::WebCompositorSupportImpl();
    return m_webCompositorSupport;
}

blink::WebScrollbarBehavior* BlinkPlatformImpl::scrollbarBehavior()
{
    if (!m_webScrollbarBehavior)
        m_webScrollbarBehavior = new blink::WebScrollbarBehavior();
    return m_webScrollbarBehavior;
}

blink::WebURLError BlinkPlatformImpl::cancelledError(const blink::WebURL& url) const
{
    blink::WebURLError error;
    error.reason = -1;
    error.domain = blink::WebString(url.string());
    error.localizedDescription = blink::WebString::fromUTF8("url cancelledError\n");

    WTF::String outError = "url cancelledError:";
    outError.append((WTF::String)url.string());
    outError.append("\n");
    OutputDebugStringW(outError.charactersWithNullTermination().data());

    return error;
}

blink::WebStorageNamespace* BlinkPlatformImpl::createLocalStorageNamespace()
{
    if (!m_localStorageStorageMap)
        m_localStorageStorageMap = new DOMStorageMapWrap();
    return new blink::WebStorageNamespaceImpl(blink::kLocalStorageNamespaceId, m_localStorageStorageMap->map);
}

blink::WebStorageNamespace* BlinkPlatformImpl::createSessionStorageNamespace()
{
    if (!m_sessionStorageStorageMap)
        m_sessionStorageStorageMap = new DOMStorageMapWrap();
    return new blink::WebStorageNamespaceImpl(m_storageNamespaceIdCount++, m_sessionStorageStorageMap->map);
}

// Resources -----------------------------------------------------------
blink::WebString BlinkPlatformImpl::queryLocalizedString(blink::WebLocalizedString::Name name)
{
    return queryLocalizedStringFromResources(name);
}

blink::WebString BlinkPlatformImpl::queryLocalizedString(blink::WebLocalizedString::Name, const blink::WebString& parameter)
{
    return blink::WebString();
}

blink::WebString BlinkPlatformImpl::queryLocalizedString(blink::WebLocalizedString::Name, const blink::WebString& parameter1, const blink::WebString& parameter2)
{
    return blink::WebString();
}

// Blob ----------------------------------------------------------------
blink::WebBlobRegistry* BlinkPlatformImpl::blobRegistry()
{
    return new WebBlobRegistryImpl();
}

} // namespace content