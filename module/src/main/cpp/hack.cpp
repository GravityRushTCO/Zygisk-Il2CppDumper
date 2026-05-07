#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <string>
#include <android/log.h>
#include <inttypes.h> // REQUIS pour PRIxPTR
#include <stdint.h>    // REQUIS pour uintptr_t

// ──────────────────────────────────────────────────────────────
// LOG TRÈS VISIBLE ET FACILE À FILTRER
// ──────────────────────────────────────────────────────────────
#define KICK_TAG "KICK_TEST_SAFE"
#define KLOG(...) __android_log_print(ANDROID_LOG_ERROR, KICK_TAG, __VA_ARGS__)

// RVA exacts de ton dump.cs (Photon.Pun.PhotonNetwork)
const uintptr_t RVA_SET_MASTER = 0x1947664;
const uintptr_t RVA_CLOSE_CONN = 0x194765C;

// Noms possibles de la lib (agressif)
static const char* lib_names[] = {
    "libil2cpp.so",
    "libmain.so",
    "libgame.so",
    "libunity.so",
    "libcore.so",
    "libnative.so",
    "libbee.so",
    NULL
};

// Signatures de types acceptant un argument
typedef bool (*SetMaster_t)(void* player);
typedef bool (*Close_t)(void* player);

// ──────────────────────────────────────────────────────────────
// Recherche lib + test kick safe (sans crash)
// ──────────────────────────────────────────────────────────────
void SafeKickTest(const char* game_data_dir) {
    KLOG("SafeKickTest démarré – thread %d", gettid());
    void* handle = NULL;
    const char** name = lib_names;

    // Recherche agressive de la lib
    while (*name) {
        handle = xdl_open(*name, RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            KLOG("LIB TROUVÉE : %s – handle %p", *name, handle);
            break;
        }
        KLOG("lib %s non trouvée", *name);
        name++;
    }

    if (!handle) {
        KLOG("AUCUNE LIB TROUVÉE – abandon");
        return;
    }

    // Init API + dump
    il2cpp_api_init(handle);
    il2cpp_dump(game_data_dir);
    KLOG("Dump IL2CPP terminé");

    // Calcul RVA
    uintptr_t base = (uintptr_t)handle;
    uintptr_t addrSet = base + RVA_SET_MASTER;
    uintptr_t addrClose = base + RVA_CLOSE_CONN;

    // CORRECTION : Utilisation de PRIxPTR pour la compatibilité 32/64 bits
    KLOG("Base lib : 0x%" PRIxPTR, base);
    KLOG("SetMasterClient addr : 0x%" PRIxPTR, addrSet);
    KLOG("CloseConnection addr : 0x%" PRIxPTR, addrClose);

    // Utilisation de reinterpret_cast pour le C++
    SetMaster_t setFunc = reinterpret_cast<SetMaster_t>(addrSet);
    Close_t closeFunc = reinterpret_cast<Close_t>(addrClose);

    KLOG("Pointeurs de fonction prêts");

    // Test appels sur pointeur fictif pour éviter crash immédiat
    void* fakePlayer = (void*)0xDEADBEEF;

    KLOG("Test SetMasterClient sur fake player...");
    bool setOk = setFunc(fakePlayer);
    KLOG("SetMasterClient → %s", setOk ? "OK" : "Échec (normal avec fake)");

    KLOG("Test CloseConnection sur fake player...");
    bool closeOk = closeFunc(fakePlayer);
    KLOG("CloseConnection → %s", closeOk ? "OK" : "Échec (normal avec fake)");

    KLOG("Test kick terminé – vérifie si le jeu crash");
    xdl_close(handle);
}

// ──────────────────────────────────────────────────────────────
// hack_start – lancement immédiat
// ──────────────────────────────────────────────────────────────
void hack_start(const char *game_data_dir) {
    KLOG("hack_start – MODE TEST SAFE – thread %d", gettid());
    SafeKickTest(game_data_dir);
    KLOG("hack_start terminé – regarde logcat avec KICK_TEST_SAFE");
}

// ──────────────────────────────────────────────────────────────
// Fonctions utilitaires JNI / NativeBridge
// ──────────────────────────────────────────────────────────────

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
            "currentApplication",
            "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz, currentApplicationId);
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                    "getApplicationInfo",
                    "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application, get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                        env->GetObjectClass(application_info), "nativeLibraryDir",
                        "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    }
                }
            }
        }
    }
    return {};
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;
    void *(*loadLibrary)(const char *libpath, int flag);
    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);
    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;
    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void* data, size_t length) {
    sleep(5);
    auto libart = dlopen("libart.so", RTLD_NOW);
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart, "JNI_GetCreatedJavaVMs");
    
    JavaVM *vms_buf[1];
    jsize num_vms;
    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
    if (status != JNI_OK || num_vms <= 0) return false;

    JavaVM *vms = vms_buf[0];
    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) return false;

    if (lib_dir.find("/lib/x86") != std::string::npos) {
        munmap(data, length);
        return false;
    }

    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }

    if (nb) {
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            ftruncate(fd, (off_t) length);
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            
            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }

            if (arm_handle) {
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle, "JNI_OnLoad", nullptr, 0);
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    int api_level = android_get_device_api_level();
#if defined(i386) || defined(x86_64)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir);
#if defined(i386) || defined(x86_64)
    }
#endif
}

#if defined(arm) || defined(aarch64)
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}
#endif
