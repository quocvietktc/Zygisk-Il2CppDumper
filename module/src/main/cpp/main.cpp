#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <fstream> // [MỚI] Để đọc file
#include <string>  // [MỚI] Để xử lý chuỗi
#include <algorithm> // [MỚI] Để xử lý chuỗi

#include "hack.h"
#include "zygisk.hpp"
#include "game.h"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// --- [MỚI] HÀM ĐỌC TARGET PACKAGE TỪ FILE ---
std::string GetTargetPackage() {
    // Đường dẫn file cấu hình do App MAUI tạo ra
    std::ifstream file("/data/local/tmp/dumper_target.txt");
    std::string target;
    
    if (file.is_open()) {
        std::getline(file, target);
        file.close();
        
        // Xử lý chuỗi: Xóa khoảng trắng hoặc xuống dòng thừa (Trim)
        // Vì lệnh 'echo' thường thêm ký tự xuống dòng \n
        const char* whitespace = " \t\n\r\f\v";
        target.erase(target.find_last_not_of(whitespace) + 1);
        target.erase(0, target.find_first_not_of(whitespace));
    }
    return target;
}

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *game_data_dir;
    void *data;
    size_t length;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        // --- [SỬA ĐỔI CHÍNH TẠI ĐÂY] ---
        
        // 1. Lấy package mục tiêu từ file
        std::string target = GetTargetPackage();

        // 2. So sánh: Nếu file target có nội dung VÀ trùng với package đang mở
        // (Thay thế cho đoạn strcmp(package_name, GamePackageName) cũ)
        if (!target.empty() && strcmp(package_name, target.c_str()) == 0) {
            
            LOGI("detect game: %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            // Nếu không phải game mục tiêu -> Tắt module để tiết kiệm pin/RAM
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
