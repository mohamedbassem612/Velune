plugins { kotlin("jvm") version "2.3.10" }
repositories { mavenCentral() }
kotlin { jvmToolchain(21) }
dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-core:1.10.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    implementation("org.json:json:20250517")
    testImplementation("junit:junit:4.13.2")
}
sourceSets {
    main {
        kotlin.srcDir("../../app/src/main/kotlin")
        kotlin.include("com/nikhil/yt/connect/ConnectPolicy.kt", "com/nikhil/yt/connect/ConnectTransfer.kt",
            "com/nikhil/yt/connect/ConnectCredentials.kt", "com/nikhil/yt/connect/ConnectTransport.kt",
            "com/nikhil/yt/connect/ConnectLibraryDelta.kt", "com/nikhil/yt/localsync/LibrarySnapshot.kt",
            "com/nikhil/yt/together/TogetherLink.kt")
    }
    test {
        kotlin.srcDir("../../app/src/test/kotlin")
        kotlin.include("com/nikhil/yt/connect/ConnectPolicyTest.kt", "com/nikhil/yt/connect/ConnectTransferTest.kt",
            "com/nikhil/yt/connect/ConnectLibraryDeltaTest.kt", "com/nikhil/yt/connect/ConnectTransportTest.kt", "com/nikhil/yt/localsync/LibraryMergeTest.kt")
    }
}
tasks.test { useJUnit() }
