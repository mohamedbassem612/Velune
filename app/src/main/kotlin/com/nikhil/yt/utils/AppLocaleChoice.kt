package com.nikhil.yt.utils

import com.nikhil.yt.constants.SYSTEM_DEFAULT
import java.util.Locale

/** A manual choice wins; otherwise Android's system locale remains the source of truth. */
internal fun chosenAppLocale(choice: String?, systemLocale: Locale): Locale =
    choice?.takeUnless { it == SYSTEM_DEFAULT || it.isBlank() }
        ?.let(Locale::forLanguageTag)
        ?.takeUnless { it.language.isBlank() }
        ?: systemLocale
