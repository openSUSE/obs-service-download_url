#include <curl/curl.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

bool link_or_copy(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    namespace fs = std::filesystem;

    try {
        fs::create_hard_link(source, destination);
    } catch (const fs::filesystem_error&) {
        try {
            fs::copy_file(
                source,
                destination,
                fs::copy_options::overwrite_existing
            );
        } catch (const fs::filesystem_error&) {
            return false;
        }
    }
    return true;
}

bool is_enabled(const string& value)
{
    if (value == "enable") {
        return true;
    }
    if (value != "disable"){
        cerr << "Unknown value '" << value
        << "'. Must be either 'enable' or 'disable'\n";
        exit(1);
    }
    return false;
}

string trim(string line)
{
    size_t first = 0;
    size_t last = line.size();
    while (first < last &&
        isspace(static_cast<unsigned char>(line[first]))) {
        ++first;
    }
    while (first < last &&
        isspace(static_cast<unsigned char>(line[last - 1]))) {
        --last;
    }
    return line.substr(first, last - first);
}

static size_t write_callback(
    void* ptr,
    size_t size,
    size_t nmemb,
    void* userdata)
{
    auto* file = static_cast<ofstream*>(userdata);

    const size_t bytes = size * nmemb;

    file->write(
        static_cast<const char*>(ptr),
                bytes
    );

    return file->good() ? bytes : 0;
}

string filename_from_url(const string& url)
{
    size_t end = url.find('?');

    if (end == string::npos)
        end = url.size();

    size_t start = url.rfind('/', end);

    if (start == string::npos)
        start = 0;
    else
        ++start;

    if (start >= end)
        return {};

    return url.substr(start, end - start);
}

bool download_file(
    CURL* curl,
    const string& url,
    const filesystem::path& output,
    bool enforceipv4,
    bool sslvalidation)
{
    cout << "Downloading: " << url << '\n';
    cout << "       -> " << output << '\n';

    ofstream file(output, ios::binary);

    if (!file) {
        cerr << "Unable to open output file: "
        << output << '\n';
        return false;
    }

    curl_easy_reset(curl);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Follow HTTP redirects.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Stream received data directly into the file.
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);

    // Force IPv4 if requested.
    curl_easy_setopt(
        curl,
        CURLOPT_IPRESOLVE,
        enforceipv4
        ? CURL_IPRESOLVE_V4
        : CURL_IPRESOLVE_WHATEVER
    );

    // TLS certificate/hostname validation.
    curl_easy_setopt(
        curl,
        CURLOPT_SSL_VERIFYPEER,
        sslvalidation ? 1L : 0L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_SSL_VERIFYHOST,
        sslvalidation ? 2L : 0L
    );

    // Useful User-Agent.
    curl_easy_setopt(
        curl,
        CURLOPT_USERAGENT,
        "obs-service-download_url/1.0"
    );

    CURLcode result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
        cerr << "Download failed: "
        << curl_easy_strerror(result)
        << '\n';

        file.close();

        // Don't leave a partial file around.
        filesystem::remove(output);

        return false;
    }

    long response_code = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &response_code
    );

    file.close();

    if (response_code < 200 || response_code >= 300) {
        cerr << "HTTP error "
        << response_code
        << " for " << url << '\n';

        filesystem::remove(output);

        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    argv++;
    argc--;

    string key;

    string host;
    int port = -1;

    string path;
    string schema = "http";

    filesystem::path outdir;

    string filename;
    string url;

    vector<string> manifest;

    bool enforceipv4 = true;
    bool sslvalidation = true;
    bool prefer_old = false;

    for (int i = 0; i < argc; ++i) {
        if ((i % 2) == 0) {
            key = argv[i];
            size_t c = 0;
            while (c < key.size() && key[c] == '-') {
                c++;
            }
            key = key.substr(c);
        } else {
            const string value = argv[i];

            if (key == "host") {
                host = value;
            } else if (key == "port") {
                port = stoi(value);
            } else if (key == "protocol") {
                schema = value;
            } else if (key == "path") {
                path = value;
            } else if (key == "filename") {
                filename = value;
            } else if (key == "outdir") {
                outdir = value;
            } else if (key == "url") {
                if (!manifest.empty()){
                    manifest.push_back(value);
                } else if (url.empty()){
                    url = value;
                } else {
                    manifest.push_back(url);
                    manifest.push_back(value);
                }
            } else if (key == "prefer-old") {
                prefer_old = is_enabled(value);
            } else if (key == "enforceipv4") {
                enforceipv4 = is_enabled(value);
            } else if (key == "sslvalidation") {
                sslvalidation = is_enabled(value);
            } else if (key == "download-manifest") {
                ifstream file(value);
                if (!file) {
                    cerr << "Unable to open manifest: " << value << endl;
                    return 1;
                }

                string line;

                while (getline(file, line)) {
                    line = trim(line);
                    if (!line.empty() && line[0] != '#') {
                        manifest.push_back(line );
                    }
                }
            } else {
                cerr << "Unknown parameter " << key << "." << endl;
                return 1;
            }
        }
    }
    vector<string> urls;
    if (outdir.empty()){
        cerr << "ERROR: no output directory is given via --outdir parameter!" << endl;
        return 1;
    }

    if (!manifest.empty()) {
        urls = manifest;
    } else {
        if (url.empty()){
            if (host.empty() ||
                path.empty()) {
                cerr << "ERROR: need url or host, path " << endl;
                return 1;
            }
            url =
            schema + "://" + host;

            if (port != -1){
                url += ":" + to_string(port);
            }

            if (!path.empty() && path.front() != '/'){
                url += '/';
            }
            url += path;
        }
        urls.push_back(url);
    }

    filesystem::create_directories(outdir);

    const bool custom_filename = manifest.empty() && !filename.empty();

    curl_global_init(CURL_GLOBAL_DEFAULT);

    CURL* curl = curl_easy_init();

    if (!curl) {
        cerr << "Unable to initialize libcurl\n";
        curl_global_cleanup();
        return 1;
    }

    int failures = 0;

    for (const string& current_url : urls) {
        string output_name;

        if (custom_filename) {
            output_name = filename;
        } else {
            output_name = filename_from_url(current_url);

            if (output_name.empty()) {
                cerr << "ERROR: can't determine file name from " << current_url << endl;
                ++failures;
                continue;
            }
        }

        filesystem::path output =
        outdir / output_name;

        if (prefer_old){
            if (urls.size() == 1){
                if (link_or_copy(".old/_service:download_url:"+output_name, output)){
                    continue;
                };
            } else {
                prefer_old = false;
            }
        }

        if (!download_file(
            curl,
            current_url,
            output,
            enforceipv4,
            sslvalidation)) {
            ++failures;
        }
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return failures ? 1 : 0;
}
