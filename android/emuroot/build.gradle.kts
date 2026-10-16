plugins {
    id("com.android.library")
}

android {
    namespace = "io.github.mkalmousli.emuroot"
    compileSdk = 35
    ndkVersion = "28.1.13356709"            // r28: 16 KB page-size aligned libraries by default

    defaultConfig {
        // The library itself needs Android 5 (API 21); the NDK no longer targets older.
        // For older Android use the static command-line binaries, see docs/android.md.
        minSdk = 21
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")
        ndk {
            // All four ABIs Google Play expects (64-bit ones are mandatory).
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    testOptions {
        targetSdk = 35                      // test like a current Play app
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    publishing {
        singleVariant("release") { withSourcesJar() }
    }
}

dependencies {
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
