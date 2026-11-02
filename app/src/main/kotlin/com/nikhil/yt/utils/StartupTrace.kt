package com.nikhil.yt.utils

import android.os.Process
import android.os.SystemClock
import timber.log.Timber

/** Logs how many milliseconds passed since the app process started. Used to find slow startup steps. */
object StartupTrace {
    fun mark(name: String) {
        val sinceStart = SystemClock.elapsedRealtime() - Process.getStartElapsedRealtime()
        Timber.tag("StartupTrace").i("$name +${sinceStart}ms since process start (thread=${Thread.currentThread().name})")
    }
}
