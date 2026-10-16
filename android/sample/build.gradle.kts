plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.mkalmousli.emuroot.sample"
    compileSdk = 35
    ndkVersion = "28.1.13356709"

    defaultConfig {
        applicationId = "io.github.mkalmousli.emuroot.sample"
        minSdk = 21
        targetSdk = 35                      // a current Google Play configuration
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            // Keeps the JNI entry points: see emuroot/consumer-rules.pro
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
}

dependencies {
    implementation(project(":emuroot"))
}
