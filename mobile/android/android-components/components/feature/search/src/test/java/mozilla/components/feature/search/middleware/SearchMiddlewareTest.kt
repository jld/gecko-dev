/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.middleware

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.TestDispatcher
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.availableSearchEngines
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.search.ext.createSearchEngine
import mozilla.components.feature.search.storage.CustomSearchEngineStorage
import mozilla.components.feature.search.storage.SearchMetadataStorage
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.fakes.android.FakeSharedPreferences
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoMoreInteractions
import java.util.Locale
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class SearchMiddlewareTest {
    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()
    private val dispatcher = coroutinesTestRule.testDispatcher

    private lateinit var originalLocale: Locale

    @Before
    fun setUp() {
        originalLocale = Locale.getDefault()
    }

    @After
    fun tearDown() {
        if (Locale.getDefault() != originalLocale) {
            Locale.setDefault(originalLocale)
        }
    }

    @Test
    fun `Loads search engines for locale (US)`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("US", "US"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertNotNull(store.state.search.regionSearchEngines.find { it.name == "Google" })
        assertNull(store.state.search.regionSearchEngines.find { it.name == "Yandex" })
    }

    @Test
    fun `WHEN distribution doesn't exist THEN Loads default search engines`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("US", "US"),
                "test",
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertNotNull(store.state.search.regionSearchEngines.find { it.name == "Google" })
        assertNull(store.state.search.regionSearchEngines.find { it.name == "Yandex" })
    }

    fun `Loads search engines for locale (An)`() {
        Locale.setDefault(Locale.Builder().setLanguage("an").setRegion("AN").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("AN", "AN"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (CA)`() {
        Locale.setDefault(Locale.Builder().setLanguage("CA").setRegion("CA").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("CA", "CA"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[3].name)
        assertEquals("Viquipèdia (ca)", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (CY)`() {
        Locale.setDefault(Locale.Builder().setLanguage("cy").setRegion("CY").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("CY", "CY"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wicipedia (cy)", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (fy-NL)`() {
        Locale.setDefault(Locale.Builder().setLanguage("fy").setRegion("NL").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("FY", "NL"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia (fy)", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (en-AU)`() {
        Locale.setDefault(Locale.Builder().setLanguage("en").setRegion("AU").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("EN", "AU"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (en-GB)`() {
        Locale.setDefault(Locale.Builder().setLanguage("en").setRegion("GB").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("EN", "GB"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(6, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Qwant", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[4].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[5].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (en-IE)`() {
        Locale.setDefault(Locale.Builder().setLanguage("en").setRegion("IE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("EN", "IE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(6, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Qwant", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[4].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[5].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (fr-BE)`() {
        Locale.setDefault(Locale.Builder().setLanguage("fr").setRegion("BE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("FR", "BE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(6, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Qwant", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipédia (fr)", store.state.search.regionSearchEngines[4].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[5].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (fr-CA)`() {
        Locale.setDefault(Locale.Builder().setLanguage("fr").setRegion("CA").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("FR", "CA"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipédia (fr)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (fr-FR)`() {
        Locale.setDefault(Locale.Builder().setLanguage("fr").setRegion("FR").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("FR", "FR"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(6, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Qwant", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipédia (fr)", store.state.search.regionSearchEngines[4].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[5].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (de-AT)`() {
        Locale.setDefault(Locale.Builder().setLanguage("de").setRegion("AT").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("DE", "AT"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(7, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Ecosia", store.state.search.regionSearchEngines[3].name)
        assertEquals("Qwant", store.state.search.regionSearchEngines[4].name)
        assertEquals("Wikipedia (de)", store.state.search.regionSearchEngines[5].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[6].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (DE)`() {
        Locale.setDefault(Locale.Builder().setLanguage("de").setRegion("DE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("DE", "DE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(7, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Ecosia", store.state.search.regionSearchEngines[3].name)
        assertEquals("Qwant", store.state.search.regionSearchEngines[4].name)
        assertEquals("Wikipedia (de)", store.state.search.regionSearchEngines[5].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[6].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (DSB)`() {
        Locale.setDefault(Locale.Builder().setLanguage("dsb").setRegion("DE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("DSB", "DE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedija (dsb)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (HSB)`() {
        Locale.setDefault(Locale.Builder().setLanguage("hsb").setRegion("DE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("HSB", "DE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedija (hsb)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (ES)`() {
        Locale.setDefault(Locale.Builder().setLanguage("es").setRegion("ES").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("ES", "ES"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedia (es)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (IT)`() {
        Locale.setDefault(Locale.Builder().setLanguage("it").setRegion("IT").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("it", "IT"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedia (it)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for Locale (lij)`() {
        Locale.setDefault(Locale.Builder().setLanguage("lij").setRegion("ZE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("lij", "ZE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedia (lij)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (SE)`() {
        Locale.setDefault(Locale.Builder().setLanguage("sv").setRegion("SE").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("sv", "SE"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(6, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("Prisjakt", store.state.search.regionSearchEngines[2].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia (sv)", store.state.search.regionSearchEngines[4].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[5].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (PL)`() {
        Locale.setDefault(Locale.Builder().setLanguage("pl").setRegion("PL").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("pl", "PL"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("Wikipedia (pl)", store.state.search.regionSearchEngines[3].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (RU)`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("RU", "RU"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertNull(store.state.search.regionSearchEngines.find { it.name == "Yandex" })
        assertNotNull(store.state.search.regionSearchEngines.find { it.name == "Google" })
        assertNotNull(store.state.search.regionSearchEngines.find { it.name == "DuckDuckGo" })
    }

    @Test
    fun `Loads additional search engines`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            additionalBundledSearchEngineIds = listOf("reddit", "youtube"),
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("US", "US"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(2, store.state.search.additionalAvailableSearchEngines.size)

        val first = store.state.search.additionalAvailableSearchEngines[0]
        assertEquals("Reddit", first.name)
        assertEquals("reddit", first.id)

        val second = store.state.search.additionalAvailableSearchEngines[1]
        assertEquals("YouTube", second.name)
        assertEquals("youtube", second.id)

        assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
        assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

        assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
        assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })
    }

    @Test
    fun `Loads additional search engine and honors user choice`() = runTestOnMain {
        val metadataStorage = SearchMetadataStorage(testContext, preferences = lazy { FakeSharedPreferences() })
        metadataStorage.setAdditionalSearchEngines(listOf("reddit"))

        val searchMiddleware = SearchMiddleware(
            testContext,
            additionalBundledSearchEngineIds = listOf("reddit", "youtube"),
            metadataStorage = metadataStorage,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("US", "US"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isNotEmpty())

        assertEquals(1, store.state.search.additionalAvailableSearchEngines.size)
        assertEquals(1, store.state.search.additionalSearchEngines.size)

        val additional = store.state.search.additionalSearchEngines[0]
        assertEquals("Reddit", additional.name)
        assertEquals("reddit", additional.id)

        val available = store.state.search.additionalAvailableSearchEngines[0]
        assertEquals("YouTube", available.name)
        assertEquals("youtube", available.id)

        assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
        assertNotNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

        assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
        assertNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })
    }

    @Test
    fun `Loads custom search engines`() = runTestOnMain {
        val searchEngine = SearchEngine(
            id = "test-search",
            name = "Test Engine",
            icon = mock(),
            type = SearchEngine.Type.CUSTOM,
            resultUrls = listOf(),
            suggestUrl = null,
        )

        val storage = CustomSearchEngineStorage(testContext, dispatcher)
        storage.saveSearchEngine(searchEngine)

        val store = BrowserStore(
            middleware = listOf(
                SearchMiddleware(
                    testContext,
                    ioDispatcher = dispatcher,
                    customStorage = storage,
                ),
            ),
        )

        store.dispatch(
            SearchAction.SetRegionAction(RegionState.Default),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.customSearchEngines.isNotEmpty())
        assertNull(store.state.search.userSelectedSearchEngineId)
    }

    @Test
    fun `Loads default search engine ID`() = runTestOnMain {
        val storage = SearchMetadataStorage(testContext)
        storage.setUserSelectedSearchEngine("test-id", null)

        val middleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            metadataStorage = storage,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(middleware),
        )

        store.dispatch(
            SearchAction.SetRegionAction(RegionState.Default),
        ).joinBlocking()

        wait(store, dispatcher)

        assertEquals("test-id", store.state.search.userSelectedSearchEngineId)
    }

    @Test
    fun `Update default search engine`() {
        val storage = SearchMetadataStorage(testContext)
        val id = "test-id-${UUID.randomUUID()}"

        run {
            val store = BrowserStore(
                middleware = listOf(
                    SearchMiddleware(
                        testContext,
                        ioDispatcher = dispatcher,
                        metadataStorage = storage,
                        customStorage = CustomSearchEngineStorage(testContext, dispatcher),
                    ),
                ),
            )

            store.dispatch(
                SearchAction.SetRegionAction(RegionState.Default),
            ).joinBlocking()

            wait(store, dispatcher)

            assertNull(store.state.search.userSelectedSearchEngineId)

            store.dispatch(SearchAction.SelectSearchEngineAction(id, null)).joinBlocking()

            wait(store, dispatcher)

            assertEquals(id, store.state.search.userSelectedSearchEngineId)
        }

        run {
            val store = BrowserStore(
                middleware = listOf(
                    SearchMiddleware(
                        testContext,
                        ioDispatcher = dispatcher,
                        metadataStorage = storage,
                        customStorage = CustomSearchEngineStorage(testContext, dispatcher),
                    ),
                ),
            )

            store.dispatch(
                SearchAction.SetRegionAction(RegionState.Default),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals(id, store.state.search.userSelectedSearchEngineId)
        }
    }

    @Test
    fun `Updates and persists additional search engines`() {
        val storage = SearchMetadataStorage(testContext, preferences = lazy { FakeSharedPreferences() })
        val middleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            metadataStorage = storage,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
            additionalBundledSearchEngineIds = listOf(
                "reddit",
                "youtube",
            ),
        )

        // First run: Add additional search engine
        run {
            val store = BrowserStore(middleware = listOf(middleware))

            store.dispatch(
                SearchAction.SetRegionAction(RegionState.Default),
            ).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalSearchEngines.isEmpty())

            assertEquals(2, store.state.search.additionalAvailableSearchEngines.size)

            val first = store.state.search.additionalAvailableSearchEngines[0]
            assertEquals("Reddit", first.name)
            assertEquals("reddit", first.id)

            val second = store.state.search.additionalAvailableSearchEngines[1]
            assertEquals("YouTube", second.name)
            assertEquals("youtube", second.id)

            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            store.dispatch(
                SearchAction.AddAdditionalSearchEngineAction("youtube"),
            ).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())

            assertEquals(1, store.state.search.additionalSearchEngines.size)
            assertEquals(1, store.state.search.additionalAvailableSearchEngines.size)

            assertEquals("Reddit", store.state.search.additionalAvailableSearchEngines[0].name)
            assertEquals("reddit", store.state.search.additionalAvailableSearchEngines[0].id)

            assertEquals("YouTube", store.state.search.additionalSearchEngines[0].name)
            assertEquals("youtube", store.state.search.additionalSearchEngines[0].id)

            assertNotNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            assertNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })
        }

        // Second run: Restores additional search engine and removes it
        run {
            val store = BrowserStore(middleware = listOf(middleware))

            store.dispatch(
                SearchAction.SetRegionAction(RegionState.Default),
            ).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())

            assertEquals(1, store.state.search.additionalSearchEngines.size)
            assertEquals(1, store.state.search.additionalAvailableSearchEngines.size)

            assertEquals("Reddit", store.state.search.additionalAvailableSearchEngines[0].name)
            assertEquals("reddit", store.state.search.additionalAvailableSearchEngines[0].id)

            assertEquals("YouTube", store.state.search.additionalSearchEngines[0].name)
            assertEquals("youtube", store.state.search.additionalSearchEngines[0].id)

            assertNotNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            assertNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            store.dispatch(
                SearchAction.RemoveAdditionalSearchEngineAction(
                    "youtube",
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalSearchEngines.isEmpty())

            assertEquals(2, store.state.search.additionalAvailableSearchEngines.size)

            val first = store.state.search.additionalAvailableSearchEngines[0]
            assertEquals("Reddit", first.name)
            assertEquals("reddit", first.id)

            val second = store.state.search.additionalAvailableSearchEngines[1]
            assertEquals("YouTube", second.name)
            assertEquals("youtube", second.id)

            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })
        }

        // Third run: Restores without additional search engine
        run {
            val store = BrowserStore(middleware = listOf(middleware))

            store.dispatch(
                SearchAction.SetRegionAction(RegionState.Default),
            ).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalAvailableSearchEngines.isNotEmpty())
            assertTrue(store.state.search.additionalSearchEngines.isEmpty())

            assertEquals(2, store.state.search.additionalAvailableSearchEngines.size)

            val first = store.state.search.additionalAvailableSearchEngines[0]
            assertEquals("Reddit", first.name)
            assertEquals("reddit", first.id)

            val second = store.state.search.additionalAvailableSearchEngines[1]
            assertEquals("YouTube", second.name)
            assertEquals("youtube", second.id)

            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNull(store.state.search.searchEngines.find { searchEngine -> searchEngine.id == "reddit" })

            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "youtube" })
            assertNotNull(store.state.search.availableSearchEngines.find { searchEngine -> searchEngine.id == "reddit" })
        }
    }

    @Test
    fun `Custom search engines - Create, Update, Delete`() {
        runTestOnMain {
            val storage: SearchMiddleware.CustomStorage = mock()
            doReturn(emptyList<SearchEngine>()).`when`(storage).loadSearchEngineList()

            val store = BrowserStore(
                middleware = listOf(
                    SearchMiddleware(
                        testContext,
                        ioDispatcher = dispatcher,
                        customStorage = storage,
                    ),
                ),
            )

            store.dispatch(
                SearchAction.SetRegionAction(RegionState.Default),
            ).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.customSearchEngines.isEmpty())
            verify(storage).loadSearchEngineList()
            verifyNoMoreInteractions(storage)

            // Add a custom search engine

            val engine1 = SearchEngine("test-id-1", "test engine one", mock(), type = SearchEngine.Type.CUSTOM)
            store.dispatch(SearchAction.UpdateCustomSearchEngineAction(engine1)).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.customSearchEngines.isNotEmpty())
            assertEquals(1, store.state.search.customSearchEngines.size)
            verify(storage).saveSearchEngine(engine1)
            verifyNoMoreInteractions(storage)

            // Add another custom search engine

            val engine2 = SearchEngine("test-id-2", "test engine two", mock(), type = SearchEngine.Type.CUSTOM)
            store.dispatch(SearchAction.UpdateCustomSearchEngineAction(engine2)).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.customSearchEngines.isNotEmpty())
            assertEquals(2, store.state.search.customSearchEngines.size)
            verify(storage).saveSearchEngine(engine2)
            verifyNoMoreInteractions(storage)

            assertEquals("test engine one", store.state.search.customSearchEngines[0].name)
            assertEquals("test engine two", store.state.search.customSearchEngines[1].name)

            // Update first engine

            val updated = engine1.copy(
                name = "updated engine",
            )
            store.dispatch(SearchAction.UpdateCustomSearchEngineAction(updated)).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.customSearchEngines.isNotEmpty())
            assertEquals(2, store.state.search.customSearchEngines.size)
            verify(storage).saveSearchEngine(updated)
            verifyNoMoreInteractions(storage)

            assertEquals("updated engine", store.state.search.customSearchEngines[0].name)
            assertEquals("test engine two", store.state.search.customSearchEngines[1].name)

            // Remove second engine

            store.dispatch(SearchAction.RemoveCustomSearchEngineAction(engine2.id)).joinBlocking()

            wait(store, dispatcher)

            assertTrue(store.state.search.customSearchEngines.isNotEmpty())
            assertEquals(1, store.state.search.customSearchEngines.size)
            verify(storage).removeSearchEngine(engine2.id)
            verifyNoMoreInteractions(storage)

            assertEquals("updated engine", store.state.search.customSearchEngines[0].name)
        }
    }

    @Test
    fun `GIVEN disabled engines list contains elements WHEN metadata storage is created THEN the engines are disabled`() = runTestOnMain {
        val additionalBundledSearchEngineIds = setOf("reddit", "youtube")
        val metadataStorage = SearchMetadataStorage(
            testContext,
            additionalBundledSearchEngineIds,
            lazy { FakeSharedPreferences() },
        )
        val disabledSearchEngineIds = metadataStorage.getDisabledSearchEngineIds()
        assertTrue(disabledSearchEngineIds.contains("reddit"))
        assertTrue(disabledSearchEngineIds.contains("youtube"))
    }

    @Test
    fun `WHEN update disabled engine action is sent THEN search state and storage get updated`() = runTestOnMain {
        val metadataStorage = SearchMetadataStorage(testContext, preferences = lazy { FakeSharedPreferences() })
        metadataStorage.setAdditionalSearchEngines(listOf("reddit"))

        val searchMiddleware = SearchMiddleware(
            testContext,
            additionalBundledSearchEngineIds = listOf("reddit", "youtube"),
            metadataStorage = metadataStorage,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertFalse(metadataStorage.getDisabledSearchEngineIds().contains("bing"))
        assertFalse(store.state.search.disabledSearchEngineIds.contains("bing"))

        store.dispatch(
            SearchAction.UpdateDisabledSearchEngineIdsAction(
                "bing",
                false,
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(metadataStorage.getDisabledSearchEngineIds().contains("bing"))
        assertTrue(store.state.search.disabledSearchEngineIds.contains("bing"))

        store.dispatch(
            SearchAction.UpdateDisabledSearchEngineIdsAction(
                "bing",
                true,
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertFalse(metadataStorage.getDisabledSearchEngineIds().contains("bing"))
        assertFalse(store.state.search.disabledSearchEngineIds.contains("bing"))
    }

    @Test
    fun `WHEN restore hidden search engines action THEN hidden engines are added back to bundled engines list`() = runTestOnMain {
        val metadataStorage = SearchMetadataStorage(testContext, preferences = lazy { FakeSharedPreferences() })
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
            metadataStorage = metadataStorage,
        )
        val store = BrowserStore(middleware = listOf(searchMiddleware))

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("US", "US"),
            ),
        ).joinBlocking()
        wait(store, dispatcher)

        val google = store.state.search.regionSearchEngines.find { searchEngine -> searchEngine.name == "Google" }
        assertNotNull(google!!)
        assertEquals(0, store.state.search.hiddenSearchEngines.size)
        assertEquals(0, metadataStorage.getHiddenSearchEngines().size)

        store.dispatch(SearchAction.HideSearchEngineAction(google.id)).joinBlocking()
        wait(store, dispatcher)

        assertNull(store.state.search.regionSearchEngines.find { it.id == google.id })

        assertEquals(1, store.state.search.hiddenSearchEngines.size)
        assertEquals(1, metadataStorage.getHiddenSearchEngines().size)
        assertNotNull(store.state.search.hiddenSearchEngines.find { it.id == google.id })
        assertNotNull(metadataStorage.getHiddenSearchEngines().find { it == google.id })

        store.dispatch(SearchAction.RestoreHiddenSearchEnginesAction).joinBlocking()
        wait(store, dispatcher)

        assertNotNull(store.state.search.regionSearchEngines.find { it.id == google.id })

        assertEquals(0, store.state.search.hiddenSearchEngines.size)
        assertEquals(0, metadataStorage.getHiddenSearchEngines().size)
        assertNull(store.state.search.hiddenSearchEngines.find { it.id == google.id })
        assertNull(metadataStorage.getHiddenSearchEngines().find { it == google.id })
    }

    @Test
    fun `Hiding and showing search engines`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
            metadataStorage = SearchMetadataStorage(testContext),
        )

        val google = BrowserStore(middleware = listOf(searchMiddleware)).let { store ->
            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            store.state.search.regionSearchEngines.find { searchEngine -> searchEngine.name == "Google" }
        }
        assertNotNull(google!!)

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertNotNull(store.state.search.regionSearchEngines.find { it.id == google.id })
            assertEquals(0, store.state.search.hiddenSearchEngines.size)

            store.dispatch(
                SearchAction.HideSearchEngineAction(google.id),
            ).joinBlocking()

            wait(store, dispatcher)

            assertNull(store.state.search.regionSearchEngines.find { it.id == google.id })
            assertEquals(1, store.state.search.hiddenSearchEngines.size)
            assertNotNull(store.state.search.hiddenSearchEngines.find { it.id == google.id })
        }

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertNull(store.state.search.regionSearchEngines.find { it.id == google.id })
            assertEquals(1, store.state.search.hiddenSearchEngines.size)
            assertNotNull(store.state.search.hiddenSearchEngines.find { it.id == google.id })

            store.dispatch(
                SearchAction.ShowSearchEngineAction(google.id),
            ).joinBlocking()

            assertNotNull(store.state.search.regionSearchEngines.find { it.id == google.id })
            assertEquals(0, store.state.search.hiddenSearchEngines.size)
        }

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertNotNull(store.state.search.regionSearchEngines.find { it.id == google.id })
            assertEquals(0, store.state.search.hiddenSearchEngines.size)
        }
    }

    @Test
    fun `Keeps user choice based on search engine name even if search engine id changes`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
            metadataStorage = SearchMetadataStorage(testContext),
        )

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            val google = store.state.search.searchEngines.find { it.name == "Google" }
            assertNotNull(google!!)
            assertEquals("google-b-1-m", google.id)

            store.dispatch(
                SearchAction.SelectSearchEngineAction(
                    searchEngineId = "google-b-1-m",
                    searchEngineName = "Google",
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals("google-b-1-m", store.state.search.userSelectedSearchEngineId)
            assertEquals("Google", store.state.search.userSelectedSearchEngineName)

            val searchEngine = store.state.search.selectedOrDefaultSearchEngine
            assertNotNull(searchEngine!!)
            assertEquals("google-b-1-m", searchEngine.id)
            assertEquals("Google", searchEngine.name)
        }

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("DE", "DE"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals("google-b-1-m", store.state.search.userSelectedSearchEngineId)
            assertEquals("Google", store.state.search.userSelectedSearchEngineName)

            val searchEngine = store.state.search.selectedOrDefaultSearchEngine
            assertNotNull(searchEngine!!)
            assertEquals("google-b-m", searchEngine.id)
            assertEquals("Google", searchEngine.name)
        }
    }

    @Test
    fun `Adding and restoring custom search engine`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
            metadataStorage = SearchMetadataStorage(testContext),
        )

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals(0, store.state.search.customSearchEngines.size)

            store.dispatch(
                SearchAction.UpdateCustomSearchEngineAction(
                    SearchEngine(
                        id = UUID.randomUUID().toString(),
                        name = "Example",
                        icon = mock(),
                        type = SearchEngine.Type.CUSTOM,
                        resultUrls = listOf(
                            "https://example.org/?q=%s",
                        ),
                    ),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals(1, store.state.search.customSearchEngines.size)
        }

        run {
            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals(1, store.state.search.customSearchEngines.size)
        }
    }

    @Test
    fun `Migration - custom search engine and default search engine`() {
        val customStorage = CustomSearchEngineStorage(testContext, dispatcher)
        val metadataStorage = SearchMetadataStorage(testContext)

        run {
            val searchMiddleware = SearchMiddleware(
                testContext,
                ioDispatcher = dispatcher,
                customStorage = customStorage,
                metadataStorage = metadataStorage,
                migration = object : SearchMiddleware.Migration {
                    override fun getValuesToMigrate() = SearchMiddleware.Migration.MigrationValues(
                        customSearchEngines = listOf(
                            createSearchEngine(
                                name = "Example",
                                url = "https://example.org/?q={searchTerms}",
                                icon = mock(),
                            ),
                        ),
                        defaultSearchEngineName = "Example",
                    )
                },
            )

            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals(1, store.state.search.customSearchEngines.size)

            val selectedSearchEngine = store.state.search.selectedOrDefaultSearchEngine
            assertNotNull(selectedSearchEngine!!)

            assertEquals("Example", selectedSearchEngine.name)
            assertEquals("https://example.org/?q={searchTerms}", selectedSearchEngine.resultUrls[0])
        }

        run {
            val searchMiddleware = SearchMiddleware(
                testContext,
                ioDispatcher = dispatcher,
                customStorage = customStorage,
                metadataStorage = metadataStorage,
            )

            val store = BrowserStore(middleware = listOf(searchMiddleware))

            store.dispatch(
                SearchAction.SetRegionAction(
                    RegionState("US", "US"),
                ),
            ).joinBlocking()

            wait(store, dispatcher)

            assertEquals(1, store.state.search.customSearchEngines.size)

            val selectedSearchEngine = store.state.search.selectedOrDefaultSearchEngine
            assertNotNull(selectedSearchEngine!!)

            assertEquals("Example", selectedSearchEngine.name)
            assertEquals("https://example.org/?q={searchTerms}", selectedSearchEngine.resultUrls[0])
        }
    }

    @Test
    fun `Reorders list of region search engines after adding previously removed search engines`() {
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("US", "US"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        // ///////////////////////////////////////////////////////////////////////////////////////////
        // Verify initial state
        // ///////////////////////////////////////////////////////////////////////////////////////////

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)

        store.dispatch(
            SearchAction.HideSearchEngineAction(
                "google-b-1-m",
            ),
        ).joinBlocking()

        store.dispatch(
            SearchAction.HideSearchEngineAction(
                "ddg",
            ),
        ).joinBlocking()

        // ///////////////////////////////////////////////////////////////////////////////////////////
        // Verify after hiding search engines
        // ///////////////////////////////////////////////////////////////////////////////////////////

        assertEquals(3, store.state.search.regionSearchEngines.size)

        assertEquals("Bing", store.state.search.regionSearchEngines[0].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[1].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[2].name)

        assertEquals("Bing", store.state.search.selectedOrDefaultSearchEngine!!.name)

        println(store.state.search.regionSearchEngines)

        store.dispatch(
            SearchAction.ShowSearchEngineAction("google-b-1-m"),
        ).joinBlocking()

        store.dispatch(
            SearchAction.ShowSearchEngineAction("ddg"),
        ).joinBlocking()

        // ///////////////////////////////////////////////////////////////////////////////////////////
        // Verify state after adding search engines back
        // ///////////////////////////////////////////////////////////////////////////////////////////

        assertEquals(5, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[2].name)
        assertEquals("eBay", store.state.search.regionSearchEngines[3].name)
        assertEquals("Wikipedia", store.state.search.regionSearchEngines[4].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }

    @Test
    fun `Loads search engines for locale (JA)`() {
        Locale.setDefault(Locale.Builder().setLanguage("ja").setRegion("JA").build())
        val searchMiddleware = SearchMiddleware(
            testContext,
            ioDispatcher = dispatcher,
            customStorage = CustomSearchEngineStorage(testContext, dispatcher),
        )

        val store = BrowserStore(
            middleware = listOf(searchMiddleware),
        )

        assertTrue(store.state.search.regionSearchEngines.isEmpty())

        store.dispatch(
            SearchAction.SetRegionAction(
                RegionState("JA", "JA"),
            ),
        ).joinBlocking()

        wait(store, dispatcher)

        assertTrue(store.state.search.regionSearchEngines.isNotEmpty())
        assertTrue(store.state.search.additionalAvailableSearchEngines.isEmpty())
        assertTrue(store.state.search.additionalSearchEngines.isEmpty())

        assertEquals(8, store.state.search.regionSearchEngines.size)

        assertEquals("Google", store.state.search.regionSearchEngines[0].name)
        assertEquals("Bing", store.state.search.regionSearchEngines[1].name)
        assertEquals("Amazon.co.jp", store.state.search.regionSearchEngines[2].name)
        assertEquals("DuckDuckGo", store.state.search.regionSearchEngines[3].name)
        assertEquals("楽天市場", store.state.search.regionSearchEngines[4].name)
        assertEquals("Wikipedia (ja)", store.state.search.regionSearchEngines[5].name)
        assertEquals("Yahoo! JAPAN", store.state.search.regionSearchEngines[6].name)
        assertEquals("Yahoo!オークション", store.state.search.regionSearchEngines[7].name)

        assertEquals("Google", store.state.search.selectedOrDefaultSearchEngine!!.name)
    }
}

private fun wait(store: BrowserStore, dispatcher: TestDispatcher) {
    // First we wait for the InitAction that may still need to be processed.
    store.waitUntilIdle()

    // Now we wait for the Middleware that may need to asynchronously process an action the test dispatched
    dispatcher.scheduler.advanceUntilIdle()

    // Since the Middleware may have dispatched an action, we now wait for the store again.
    store.waitUntilIdle()
}
