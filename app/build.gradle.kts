plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.cepeter.telegramhider"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.cepeter.telegramhider"
        minSdk = 27
        targetSdk = 35
        versionCode = 7
        versionName = "2.0.0"

        testInstrumentationRunner = "android.test.InstrumentationTestRunner"
    }

    buildFeatures {
        aidl = true
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    compileOnly("de.robv.android.xposed:api:82")
    testImplementation("junit:junit:4.13.2")
}
