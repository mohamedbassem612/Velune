package com.nikhil.yt.utils

import com.nikhil.yt.constants.SYSTEM_DEFAULT
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Test

class AppLocaleChoiceTest {
    @Test fun systemLanguageFollowsTheTvForEnglishAndArabic() {
        assertEquals("en-US", chosenAppLocale(SYSTEM_DEFAULT, Locale.US).toLanguageTag())
        assertEquals("ar-AE", chosenAppLocale(null, Locale.forLanguageTag("ar-AE")).toLanguageTag())
    }

    @Test fun explicitLanguageOverridesTheTvAndReturningToSystemRestoresIt() {
        val system = Locale.forLanguageTag("ar-AE")
        assertEquals("en", chosenAppLocale("en", system).language)
        assertEquals(system, chosenAppLocale(SYSTEM_DEFAULT, system))
    }
}
