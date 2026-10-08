#pragma once

#include <cstddef>
#include <string>

/**
 * @brief A file of the web UI compiled into the server.
 */
struct WebAsset
{
	const char *m_path;
	const unsigned char *m_data;
	std::size_t m_size;
};

/**
 * @brief Find an embedded web UI file by URL path (e.g. "/index.html"), nullptr if there is none.
 */
const WebAsset *findWebAsset(const std::string &path);
