//
// Created by Perfare on 2020/7/4.
//
#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"

// ────────────────────────────────────────────────────────────────────────────────
// INCLUDES OBLIGATOIRES POUR LES FONCTIONS IL2CPP (corrige les "undeclared" et types inconnus)
#include "il2cpp-api-functions.h"   // Contient il2cpp_domain_get, il2cpp_domain_assembly_open, etc.
#include "il2cpp-class.h"           // Contient Il2CppClass, Il2CppMethod, Il2CppImage, etc.
#include "il2cpp-class-internals.h" // Si besoin pour Il2CppException / internals (souvent inclus)

// ────────────────────────────────────────────────────────────────────────────────
// AUTRES INCLUDES STANDARDS
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
// PARTIE PHOTON KICK TCO (corrigée)
// ────────────────────────────────────────────────────────────────────────────────

// Tag dédié pour éviter conflit avec log.h
#define TCO_LOG_TAG "TCO_KICK"
#define TCO_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TCO_LOG_TAG, __VA_ARGS__)

// RVA exacts issus de ton dump.cs
const uintptr_t RVA_SET_MASTER_CLIENT = 0x1947664;
const uintptr_t RVA_CLOSE_CONNECTION   = 0x194765C;

// Fonction pour tenter de devenir Master
bool SetMasterClient(void* localPlayer) {
    void* libil2cpp = xdl_open("libil2cpp.so", 0);
    if (!libil2cpp) {
        TCO_LOGD("Erreur: libil2cpp.so non trouvé pour SetMasterClient");
        return false;
    }

    uintptr_t base = (uintptr_t)libil2cpp;
    void* methodAddr = (void*)(base + RVA_SET_MASTER_CLIENT);

    typedef bool (*SetMasterClient_t)(void* player);
    SetMasterClient_t func = (SetMasterClient_t)methodAddr;

    bool success = func(localPlayer);
    TCO_LOGD("SetMasterClient appelé → %s", success ? "OK" : "Échec");

    xdl_close(libil2cpp);
    return success;
}

// Fonction pour kicker un joueur
bool CloseConnection(void* targetPlayer) {
    void* libil2cpp = xdl_open("libil2cpp.so", 0);
    if (!libil2cpp) {
        TCO_LOGD("Erreur: libil2cpp.so non trouvé pour CloseConnection");
        return false;
    }

    uintptr_t base = (uintptr_t)libil2cpp;
    void* methodAddr = (void*)(base + RVA_CLOSE_CONNECTION);

    typedef bool (*CloseConnection_t)(void* player);
    CloseConnection_t func = (CloseConnection_t)methodAddr;

    bool success = func(targetPlayer);
    TCO_LOGD("CloseConnection appelé → %s", success ? "OK" : "Échec");

    xdl_close(libil2cpp);
    return success;
}

// Fonction principale de test / exécution du kick
void TryPhotonKick() {
    TCO_LOGD("Photon Kick test démarré ! RVA SetMaster: 0x%x, Close: 0x%x",
             (unsigned int)RVA_SET_MASTER_CLIENT,
             (unsigned int)RVA_CLOSE_CONNECTION);

    void *libil2cpp = xdl_open("libil2cpp.so", 0);
    if (!libil2cpp) {
        TCO_LOGD("Erreur dlopen libil2cpp dans TryPhotonKick");
        return;
    }

    // Récupération du domaine IL2CPP
    void* domain = il2cpp_domain_get();
    if (!domain) {
        TCO_LOGD("Erreur: il2cpp_domain_get retourné NULL");
        xdl_close(libil2cpp);
        return;
    }

    // Ouverture de l'assembly Photon3Unity3D.dll
    Il2CppAssembly* assembly = il2cpp_domain_assembly_open(domain, "Photon3Unity3D.dll");
    if (!assembly) {
        TCO_LOGD("Erreur: assembly Photon3Unity3D introuvable");
        xdl_close(libil2cpp);
        return;
    }

    Il2CppImage* image = il2cpp_assembly_get_image(assembly);
    if (!image) {
        TCO_LOGD("Erreur: image Photon3Unity3D introuvable");
        xdl_close(libil2cpp);
        return;
    }

    // Classe PhotonNetwork
    Il2CppClass* photonNetworkClass = il2cpp_class_from_name(image, "Photon.Pun", "PhotonNetwork");
    if (!photonNetworkClass) {
        TCO_LOGD("Erreur: classe Photon.Pun.PhotonNetwork introuvable");
        xdl_close(libil2cpp);
        return;
    }

    // Récupération de LocalPlayer via get_LocalPlayer()
    Il2CppMethod* getLocalPlayerMethod = il2cpp_class_get_method_from_name(photonNetworkClass, "get_LocalPlayer", 0);
    if (!getLocalPlayerMethod) {
        TCO_LOGD("Erreur: méthode get_LocalPlayer introuvable");
        xdl_close(libil2cpp);
        return;
    }

    Il2CppException* exc = nullptr;
    void* localPlayer = il2cpp_runtime_invoke(getLocalPlayerMethod, nullptr, nullptr, &exc);
    if (exc || !localPlayer) {
        TCO_LOGD("Erreur appel get_LocalPlayer (exc: %p)", exc);
        xdl_close(libil2cpp);
        return;
    }

    TCO_LOGD("LocalPlayer récupéré avec succès: %p", localPlayer);

    // On tente de devenir Master
    bool becameMaster = SetMasterClient(localPlayer);
    TCO_LOGD("Devenu Master ? %s", becameMaster ? "OUI" : "NON");

    // Simulation kick (pas de cible réelle pour ce test)
    TCO_LOGD("Simulation kick terminée (pas de cible réelle pour l'instant)");

    xdl_close(libil2cpp);
}

// ────────────────────────────────────────────────────────────────────────────────
// FONCTION ORIGINALE hack_start (modifiée pour appeler le kick)
// ────────────────────────────────────────────────────────────────────────────────

void hack_start(const char *game_data_dir) {
    bool load = false;
    for (int i = 0; i < 10; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            il2cpp_api_init(handle);
            il2cpp_dump(game_data_dir);

            // ── AJOUT ICI : après dump réussi ──
            TCO_LOGD("IL2CPP dump terminé, lancement Photon kick test...");
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
// LE RESTE DU FICHIER RESTE INCHANGÉ
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
