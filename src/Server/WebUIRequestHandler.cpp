#include <Server/WebUIRequestHandler.h>
#include <Server/HTTPResponseHeaderWriter.h>

#include <Common/re2.h>
#include <Core/ServerSettings.h>
#include <IO/HTTPCommon.h>
#include <IO/Operators.h>
#include <IO/ReadBufferFromFile.h>
#include <IO/ReadHelpers.h>
#include <Interpreters/Context.h>
#include <Server/HTTP/WriteBufferFromHTTPServerResponse.h>
#include <ClickStackResources.generated.h>

#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Util/LayeredConfiguration.h>

#include <filesystem>


/// Embedded HTML pages
constexpr unsigned char resource_play_html[] =
{
#embed "../../programs/server/play.html"
};
constexpr unsigned char resource_dashboard_html[] =
{
#embed "../../programs/server/dashboard.html"
};
constexpr unsigned char resource_uplot_js[] =
{
#embed "../../programs/server/js/uplot.js"
};
constexpr unsigned char resource_lz_string_js[] =
{
#embed "../../programs/server/js/lz-string.js"
};
constexpr unsigned char resource_binary_html[] =
{
#embed "../../programs/server/binary.html"
};
constexpr unsigned char resource_merges_html[] =
{
#embed "../../programs/server/merges.html"
};
constexpr unsigned char resource_jemalloc_html[] =
{
#embed "../../programs/server/jemalloc.html"
};


namespace DB
{

static void handle(HTTPServerRequest & request, HTTPServerResponse & response, std::string_view html,
                   std::unordered_map<String, String> http_response_headers_override = {})
{
    applyHTTPResponseHeaders(response, http_response_headers_override);
    if (response.getContentType().empty())
        response.setContentType("text/html; charset=UTF-8");
    if (request.getVersion() == HTTPServerRequest::HTTP_1_1)
        response.setChunkedTransferEncoding(true);

    setResponseDefaultHeaders(response);
    response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
    auto wb = WriteBufferFromHTTPServerResponse(response, request.getMethod() == HTTPRequest::HTTP_HEAD);
    wb.write(html.data(), html.size());
    wb.finalize();
}

void PlayWebUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    handle(request, response, {reinterpret_cast<const char *>(resource_play_html), std::size(resource_play_html)}, http_response_headers_override);
}

void DashboardWebUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    std::string html(reinterpret_cast<const char *>(resource_dashboard_html), std::size(resource_dashboard_html));

    /// Replace a link to external JavaScript file to embedded file.
    /// This allows to open the HTML without running a server and to host it on server.
    /// Note: we can embed the JavaScript file inline to the HTML,
    /// but we don't do it to keep the "view-source" perfectly readable.

    static re2::RE2 uplot_url = R"(https://[^\s"'`]+u[Pp]lot[^\s"'`]*\.js)";
    RE2::Replace(&html, uplot_url, "/js/uplot.js");

    static re2::RE2 lz_string_url = R"(https://[^\s"'`]+lz-string[^\s"'`]*\.js)";
    RE2::Replace(&html, lz_string_url, "/js/lz-string.js");

    handle(request, response, html, http_response_headers_override);
}

void BinaryWebUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    handle(request, response, {reinterpret_cast<const char *>(resource_binary_html), std::size(resource_binary_html)}, http_response_headers_override);
}

void MergesWebUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    handle(request, response, {reinterpret_cast<const char *>(resource_merges_html), std::size(resource_merges_html)}, http_response_headers_override);
}

void JavaScriptWebUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    if (request.getURI() == "/js/uplot.js")
    {
        handle(request, response, {reinterpret_cast<const char *>(resource_uplot_js), std::size(resource_uplot_js)}, http_response_headers_override);
    }
    else if (request.getURI() == "/js/lz-string.js")
    {
        handle(request, response, {reinterpret_cast<const char *>(resource_lz_string_js), std::size(resource_lz_string_js)}, http_response_headers_override);
    }
    else
    {
        response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        *response.send() << "Not found.\n";
    }
}

void JemallocWebUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    handle(request, response, {reinterpret_cast<const char *>(resource_jemalloc_html), std::size(resource_jemalloc_html)}, http_response_headers_override);
}

std::string ClickStackUIRequestHandler::getResourcePath(const std::string & uri) const
{
    std::string_view path = uri;
    if (path.starts_with("/clickstack"))
        path.remove_prefix(11); // length of "/clickstack"

    if (!path.empty() && path[0] == '/')
        path.remove_prefix(1);

    // Remove query parameters and fragments
    auto query_pos = path.find('?');
    if (query_pos != std::string_view::npos)
        path = path.substr(0, query_pos);

    auto fragment_pos = path.find('#');
    if (fragment_pos != std::string_view::npos)
        path = path.substr(0, fragment_pos);

    // Remove trailing slash
    if (!path.empty() && path.back() == '/')
        path.remove_suffix(1);

    // Handle clean URLs - map page routes to .html files
    // If path is empty or just "/", serve index.html
    if (path.empty())
        return "index.html";

    std::string path_str(path);
    if (path_str.find('.') != std::string::npos)
        return path_str;

    // assuming a path with no "." is an html page
    return path_str + ".html";
}

static std::string_view getMimeType(const std::string & path)
{
    auto ext_pos = path.rfind('.');
    if (ext_pos == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(ext_pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".html" || ext == ".htm") return "text/html; charset=UTF-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=UTF-8";
    if (ext == ".css") return "text/css; charset=UTF-8";
    if (ext == ".json") return "application/json; charset=UTF-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".webp") return "image/webp";
    if (ext == ".txt") return "text/plain; charset=UTF-8";
    if (ext == ".xml") return "application/xml; charset=UTF-8";
    return "application/octet-stream";
}

void ClickStackUIRequestHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event &)
{
    // Get the resource path from URI
    std::string resource_path = getResourcePath(request.getURI());

    // If CLICKSTACK_DIR is set, serve non-gzipped files from that directory
    const char * clickstack_dir = getenv("CLICKSTACK_DIR"); // NOLINT(concurrency-mt-unsafe)
    if (clickstack_dir)
    {
        std::filesystem::path base_dir(clickstack_dir);
        std::filesystem::path file_path = base_dir / resource_path;

        // Resolve to canonical path and verify it stays within base_dir to prevent path traversal
        std::error_code ec;
        auto canonical_path = std::filesystem::canonical(file_path, ec);
        auto canonical_base = std::filesystem::canonical(base_dir, ec);

        if (ec || !canonical_path.string().starts_with(canonical_base.string()))
        {
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            *response.send() << "Not found.\n";
            return;
        }

        if (!std::filesystem::exists(canonical_path, ec) || std::filesystem::is_directory(canonical_path, ec))
        {
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            *response.send() << "Not found.\n";
            return;
        }

        response.setContentType(std::string(getMimeType(resource_path)));

        // Read file contents and serve without gzip encoding
        std::string content;
        ReadBufferFromFile in(canonical_path.string());
        readStringUntilEOF(content, in);

        handle(request, response, content, http_response_headers_override);
        return;
    }

    // Binary search in the sorted embedded_resources array
    auto it = std::lower_bound(
        ClickStack::embedded_resources.begin(),
        ClickStack::embedded_resources.end(),
        resource_path,
        [](const ClickStack::EmbeddedResource & resource, const std::string & path)
        {
            return resource.path < path;
        });

    // Check if resource was found
    if (it == ClickStack::embedded_resources.end() || it->path != resource_path)
    {
        response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        *response.send() << "Not found.\n";
        return;
    }

    response.setContentType(std::string(it->mime_type));

    // Add Content-Encoding header since all clickstack resources are pre-gzipped
    auto headers_with_encoding = http_response_headers_override;
    headers_with_encoding["Content-Encoding"] = "gzip";

    handle(request, response, {reinterpret_cast<const char *>(it->data), it->size}, headers_with_encoding);
}

}
