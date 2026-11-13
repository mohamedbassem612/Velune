package com.nikhil.yt.connect

/** Safety boundary: a new renderer cannot activate before readiness and source release. */
internal class ConnectTransfer(val source: String, val destination: String, val epoch: Long) {
    enum class Stage { Preparing, Ready, Releasing, Released, Committed, Aborted }
    var stage = Stage.Preparing
        private set
    init { require(source != destination && epoch >= 0) }
    fun ready() { check(stage == Stage.Preparing); stage = Stage.Ready }
    fun releasing() { check(stage == Stage.Ready); stage = Stage.Releasing }
    fun released() { check(stage == Stage.Releasing); stage = Stage.Released }
    fun commit(currentRenderer: String, currentEpoch: Long): Long {
        check(stage == Stage.Released && source == currentRenderer && epoch == currentEpoch)
        stage = Stage.Committed
        return epoch + 1
    }
    /** Once release may have reached the source, an uncertain result must never auto-resume it. */
    fun abort(): Boolean {
        check(stage != Stage.Committed)
        val safeToKeepSource = stage == Stage.Preparing || stage == Stage.Ready
        stage = Stage.Aborted
        return safeToKeepSource
    }
}
