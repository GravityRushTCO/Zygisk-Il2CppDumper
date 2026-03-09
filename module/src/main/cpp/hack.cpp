//
// Created by Perfare on 2020/7/4.
//
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

// ────────────────────────────────────────────────────────────────────────────────
// PARTIE PHOTON KICK TCO – VERSION SIMPLE & SÛRE (sans dépendance il2cpp-api)
// ────────────────────────────────────────────────────────────────────────────────

// Tag dédié (pas de conflit avec log.h)
#define TCO_LOG_TAG "TCO_KICK"
#define TCO_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TCO_LOG_TAG, __VA_ARGS__)

// RVA exacts de ton dump.cs
const uintptr_t RVA_SET_MASTER_CLIENT = 0x1947664;
const uintptr_t RVA_CLOSE_CONNECTION   = 0x194765C;

// Test simple : on log + on essaie dlopen plusieurs fois
void TryPhotonKick() {
    TCO_LOGD("=== Début Photon Kick test ===");
    TCO_LOGD("RVA SetMasterClient: 0x%x", (unsigned int)RVA_SET_MASTER_CLIENT);
    TCO_LOGD("RVA CloseConnection  : 0x%x", (unsigned int)RVA_CLOSE_CONNECTION);

    // On tente dlopen plusieurs fois pour être sûr
    void* libil2cpp = nullptr;
    for (int attempt = 1; attempt <= 5; attempt++) {
        libil2cpp = xdl_open("libil2cpp.so", 0);
        if (libil2cpp) {
            TCO_LOGD("libil2cpp.so chargé avec succès (tentative %d)", attempt);
            break;
        }
        TCO_LOGD("Tentative %d échouée, attente 500ms...", attempt);
        usleep(500000);
    }

    if (!libil2cpp) {
        TCO_LOGD("Échec définitif : impossible d'ouvrir libil2cpp.so");
        return;
    }

    // Récup base address (pour debug RVA)
    uintptr_t base = (uintptr_t)libil2cpp;
    TCO_LOGD("Adresse base de libil2cpp.so : 0x%lx", base);

    // Adresse calculée des méthodes (pour debug)
    uintptr_t addrSetMaster = base + RVA_SET_MASTER_CLIENT;
    uintptr_t addrCloseConn = base + RVA_CLOSE_CONNECTION;
    TCO_LOGD("Adresse SetMasterClient  : 0x%lx", addrSetMaster);
    TCO_LOGD("Adresse CloseConnection  : 0x%lx", addrCloseConn);

    // Pour l'instant : on ne call pas encore (risque crash si signature fausse)
    // On prépare juste les pointeurs de fonction
    typedef bool (*SetMasterClient_t)(void* player);
    typedef bool (*CloseConnection_t)(void* player);

    SetMasterClient_t setMasterFunc = (SetMasterClient_t)addrSetMaster;
    CloseConnection_t closeFunc     = (CloseConnection_t)addrCloseConn;

    TCO_LOGD("Pointeurs de fonction prêts (non appelés pour sécurité)");
    TCO_LOGD("=== Fin Photon Kick test ===");

    xdl_close(libil2cpp);
}

// ────────────────────────────────────────────────────────────────────────────────
// hack_start – on appelle le test après le dump
// ────────────────────────────────────────────────────────────────────────────────

void hack_start(const char *game_data_dir) {
    bool load = false;
    for (int i = 0; i < 10; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            il2cpp_api_init(handle);
            il2cpp_dump(game_data_dir);

            // ── AJOUT : lancement du test Photon kick ──
            TCO_LOGD("IL2CPP dump terminé → lancement test Photon kick...");
            TryPhotonKick();

            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        LOGI("libil2cpp.so not found in thread %d", gettid());
    }
}

// ────────────────────────────────────────────────────────────────────────────────
// LE RESTE DU FICHIER EST INCHANGÉ (code original)
// ────────────────────────────────────────────────────────────────────────────────

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                  "getApplicationInfo",
                                                                  "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application,
                                                                     get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                            env->GetObjectClass(application_info), "nativeLibraryDir",
                            "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    } else {
                        LOGE("nativeLibraryDir not found");
                    }
                } else {
                    LOGE("getApplicationInfo not found");
                }
            } else {
                LOGE("application class not found");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
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

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    sleep(5);
    auto libart = dlopen("libart.so", RTLD_NOW);
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart,
                                                                             "JNI_GetCreatedJavaVMs");
    LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);
    JavaVM *vms_buf[1];
    JavaVM *vms;
    jsize num_vms;
    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
    if (status == JNI_OK && num_vms > 0) {
        vms = vms_buf[0];
    } else {
        LOGE("GetCreatedJavaVMs error");
        return false;
    }
    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        munmap(data, length);
        return false;
    }
    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);
            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            ftruncate(fd, (off_t) length);
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);
            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);
#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}
#endif
