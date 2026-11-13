package com.nikhil.yt.connect

import org.junit.Assert.*
import org.junit.Test

class ConnectPolicyTest {
    private val phone = "a".repeat(64)
    private val tv = "b".repeat(64)
    @Test fun discoveryNeverAuthorizesAnUnknownPeer() {
        assertFalse(ConnectPolicy.mayAuthenticate(false, false, tv, tv))
    }
    @Test fun replacementWithSameNameCannotUseOldPin() {
        assertFalse(ConnectPolicy.mayAuthenticate(true, false, tv, phone))
        assertFalse(ConnectPolicy.mayAuthenticate(false, true, tv, phone))
    }
    @Test fun trustedPeerReauthenticatesWithoutPairingWindow() {
        assertTrue(ConnectPolicy.mayAuthenticate(true, false, tv, tv))
    }
    @Test fun pairingWindowAllowsOnlyCandidateAuthenticationNotPersistedTrust() {
        assertTrue(ConnectPolicy.mayAuthenticate(false, true, tv, tv))
        assertFalse(ConnectPolicy.mayAuthenticate(false, false, tv, tv))
    }
    @Test fun codeMatchesAtBothEndsAndBindsBothKeysAndNonces() {
        val code = ConnectPolicy.pairingCode(phone, tv, "nonce-a", "nonce-b")
        assertEquals(code, ConnectPolicy.pairingCode(tv, phone, "nonce-b", "nonce-a"))
        assertNotEquals(code, ConnectPolicy.pairingCode(phone, tv, "old-nonce", "nonce-b"))
        assertNotEquals(code, ConnectPolicy.pairingCode("c".repeat(64), tv, "nonce-a", "nonce-b"))
    }
    @Test fun retryBackoffIsBounded() {
        assertEquals(1000L, ConnectPolicy.retryDelay(0, 0))
        assertEquals(2000L, ConnectPolicy.retryDelay(1, 0))
        assertEquals(30_000L, ConnectPolicy.retryDelay(100, 999))
    }
    @Test fun duplicateCommandCannotExecuteTwiceWithinTheWindow() {
        val commands = ConnectCommandWindow()
        assertTrue(commands.accept("command")); assertFalse(commands.accept("command"))
        assertTrue(commands.accept("another"))
    }

    @Test fun togetherBlocksConnectUntilExplicitEndEvenDuringAsyncStartup() {
        val mode = ConnectModeGate()
        assertTrue(mode.connectAllowed)
        mode.enterTogether()
        assertFalse(mode.connectAllowed)
        mode.enterTogether()
        assertFalse(mode.connectAllowed)
        mode.leaveTogether()
        assertTrue(mode.connectAllowed)
    }

    @Test fun commitmentBindsNonceToAuthenticatedIdentityBeforeReveal() {
        val commitment = ConnectPolicy.nonceCommitment(phone, "nonce-a")
        assertNotEquals(commitment, ConnectPolicy.nonceCommitment(phone, "nonce-b"))
        assertNotEquals(commitment, ConnectPolicy.nonceCommitment(tv, "nonce-a"))
        assertEquals(commitment, ConnectPolicy.nonceCommitment(phone, "nonce-a"))
    }
}
