// Build settings for the patcher APP (not the payload library).
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// ---- Update support -------------------------------------------------------------
// The GitHub build passes the build number in (-PtzVersionCode=...). Every build must have a
// HIGHER number than the one before, or Android refuses to update over it.
val tzVersionCode: Int = (project.findProperty("tzVersionCode") as String?)?.toIntOrNull() ?: 1
val tzVersionName: String = (project.findProperty("tzVersionName") as String?) ?: "0.2.0-local"

// The fixed signing key. The GitHub build sets these two when the TZ_KEYSTORE_B64 secret exists.
// Without them the build still works (Android's random debug key), but Update cannot install it.
val tzKeystorePath: String? = System.getenv("TZ_KEYSTORE_PATH")
val tzKeystorePass: String? = System.getenv("TZ_KEYSTORE_PASSWORD")
val tzHasKey: Boolean = !tzKeystorePath.isNullOrBlank() && !tzKeystorePass.isNullOrBlank() &&
    File(tzKeystorePath!!).exists()

android {
    namespace = "com.timmyzstuff.patcher"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.timmyzstuff.patcher"
        minSdk = 30          // Quest system software is Android 12-based
        targetSdk = 34
        versionCode = tzVersionCode
        versionName = tzVersionName
    }

    signingConfigs {
        if (tzHasKey) {
            create("tz") {
                storeFile = File(tzKeystorePath!!)
                storeType = "pkcs12"
                storePassword = tzKeystorePass
                keyAlias = "timmyzstuff"
                keyPassword = tzKeystorePass
            }
        }
    }

    buildTypes {
        debug {
            if (tzHasKey) signingConfig = signingConfigs.getByName("tz")
        }
        release {
            isMinifyEnabled = false
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}
// No extra libraries needed: the app only uses what Android already provides.
