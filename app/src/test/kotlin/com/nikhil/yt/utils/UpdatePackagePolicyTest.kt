package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class UpdatePackagePolicyTest {
    @Test fun tvAndDebugPackages_cannotUpdateMobileRelease() {
        assertFalse(isCompatibleUpdatePackage("com.nikhil.yt", 10308, "com.nikhil.yt.tv", 10309))
        assertFalse(isCompatibleUpdatePackage("com.nikhil.yt", 10308, "com.nikhil.yt.debug", 10309))
        assertFalse(isCompatibleUpdatePackage("com.nikhil.yt.tv", 10308, "com.nikhil.yt", 10309))
        assertFalse(isCompatibleUpdatePackage("com.nikhil.yt", 10308, null, 0))
    }
    @Test fun matchingPackage_acceptsUpgradesAndReinstalls_butNotDowngrades() {
        assertTrue(isCompatibleUpdatePackage("com.nikhil.yt", 10308, "com.nikhil.yt", 10309))
        assertTrue(isCompatibleUpdatePackage("com.nikhil.yt", 10309, "com.nikhil.yt", 10309))
        assertFalse(isCompatibleUpdatePackage("com.nikhil.yt", 10309, "com.nikhil.yt", 10308))
    }
}
