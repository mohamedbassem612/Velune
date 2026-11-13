package com.nikhil.yt.connect

import org.junit.Assert.*
import org.junit.Test

class ConnectTransferTest {
    @Test fun bothTransferDirectionsRequireReadyAndSourceRelease() {
        for ((source, destination) in listOf("phone" to "tv", "tv" to "phone")) {
            val transfer = ConnectTransfer(source, destination, 4)
            transfer.ready(); transfer.releasing(); transfer.released()
            assertEquals(5, transfer.commit(source, 4))
        }
    }
    @Test(expected = IllegalStateException::class) fun cannotActivateAnUnpreparedDestination() {
        ConnectTransfer("phone", "tv", 1).commit("phone", 1)
    }
    @Test fun interruptionDuringPreparationLeavesSourceUntouched() {
        assertTrue(ConnectTransfer("phone", "tv", 1).abort())
    }
    @Test fun lostReleaseAcknowledgementMustNotResumeSourceAutomatically() {
        val transfer = ConnectTransfer("tv", "phone", 1)
        transfer.ready(); transfer.releasing()
        assertFalse(transfer.abort())
    }
    @Test(expected = IllegalStateException::class) fun staleOwnershipCannotCommit() {
        val transfer = ConnectTransfer("phone", "tv", 2)
        transfer.ready(); transfer.releasing(); transfer.released()
        transfer.commit("phone", 3)
    }
}
