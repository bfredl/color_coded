#include "core.hpp"
extern "C" {
#include "nvim_api.h"
}

#include <thread>
#include <unordered_map>
#include <condition_variable>
#include <assert.h>
color_coded_callback_T registered_callback;


void do_work(int bufid, std::string &name, std::string &code, color_coded_callback_T callback) {
    using namespace color_coded;
    using namespace color_coded::core;

    try
    {
        /* Build the compiler arguments. */
        static conf::args_t const config_args_impl
        { conf::load(conf::find(".")) };

        fs::path const path{ name };
        conf::args_t config_args{ config_args_impl };
        config_args.emplace_back("-I" +
                fs::absolute(path.parent_path()).string());

        std::string const filename{ temp_dir() + path.filename().string() };
        async::temp_file tmp{ filename, code };

        /* Attempt compilation. */
        clang::translation_unit trans_unit
        { clang::compile({ config_args }, filename) };
        clang::token_pack tp{ trans_unit, clang::source_range(trans_unit) };

        vim::highlight_group hg(trans_unit, tp);
        size_t len = hg.size();
        if (len == 0) {
            callback(bufid, nullptr, 0);
            return;
        }
        highlight_t* cgrps = new highlight_t[len];
        size_t i = 0;
        for (auto & hl : hg) {
            cgrps[i++] = highlight_t{ hl.line, hl.column, hl.token.size(), hl.type.data() };
        }
        callback(bufid, cgrps, len);
        delete cgrps;
    }
    catch(clang::compilation_error const &e)
    {
        callback(bufid, nullptr, 0);
    }
    catch(...)
    {
        callback(bufid, nullptr, 0);
    }
}
// TODO: factor out 1-length result-less work-replacing "queue"
class Buffer {
public:
    Buffer(int bufid) : id(bufid), worker { std::bind(&Buffer::work, this) } {}
    // TODO: deallocate and kill;
    ~Buffer() { assert(false); };

    void request(color_coded_callback_T callback, std::string file, std::string data) {
        std::lock_guard<std::mutex> const lock{ mut };
        scheduled_file = std::move(file);
        scheduled_data =  std::move(data);
        cb = callback;
        cv.notify_one();
    }

private:
    void work() {
        while (true) {
            std::string file;
            std::string data;
            color_coded_callback_T callback;
            {
                std::unique_lock<std::mutex> lock{ mut };
                cv.wait(lock, [&]{ return !scheduled_file.empty(); });
                file = std::move(scheduled_file);
                data = std::move(scheduled_data);
                callback = cb;
                scheduled_file.clear(); // standard doesn't guarantee this is empty now
            }

            do_work(id, file, data, callback);
        }
    }

    int id;
    std::thread worker;
    std::mutex mut;
    std::condition_variable cv;
    std::string scheduled_file, scheduled_data;
    color_coded_callback_T cb;
};


std::unordered_map<int, Buffer*> bufs;

extern "C" int color_coded_request(color_coded_callback_T callback, int bufid, char* file, char* data) {
    auto & buf = bufs[bufid];
    if(!buf) buf = new Buffer(bufid);

    buf->request(callback, file, data);
    return 1;
}



