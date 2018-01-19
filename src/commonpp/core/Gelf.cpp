/*
 * File: src/commonpp/core/LoggingInterface.cpp
 * Part of commonpp.
 *
 * Distributed under the 2-clause BSD licence (See LICENCE.TXT file at the
 * project root).
 *
 * Copyright (c) 2018 Thomas Sanchez.  All rights reserved.
 *
 */

#include "commonpp/core/LoggingInterface.hpp"
#include "commonpp/core/Utils.hpp"
#include "commonpp/core/config.hpp"
#include "commonpp/core/string/json_escape.hpp"
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>

#include <boost/log/attributes/attribute_cast.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/detail/config.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/keyword_fwd.hpp>
#include <boost/log/keywords/severity.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/utility/formatting_ostream_fwd.hpp>
#include <random>
#include <stdint.h>
#include <string>

namespace logging = boost::log;
namespace sinks = logging::sinks;
namespace expr = logging::expressions;

namespace
{

size_t to_syslog_level(commonpp::LoggingLevel level) noexcept
{
    return 7 - static_cast<size_t>(level);
}
}

namespace commonpp
{
namespace core
{

class GelfFormatter
{
    std::string host_;
    std::vector<std::pair<std::string, std::string>> static_fields_;

public:
    GelfFormatter(std::string host,
                  std::vector<std::pair<std::string, std::string>> static_fields)
    : host_(std::move(host))
    , static_fields_(std::move(static_fields))
    {
        // it might be possible to "pre-render" these to a fragment of json.
        for (auto& s : static_fields_)
        {
            s.first = commonpp::string::escape_json_string(s.first);
            s.second = commonpp::string::escape_json_string(s.second);
        }
    }

    void operator()(const logging::record_view& rec,
                    logging::formatting_ostream& strm)
    {
        strm << "{";
        strm << "\"version\" : \"1.1\",";
        strm << "\"host\" : \"" << host_ << "\",";
        for (auto s : static_fields_)
        {
            strm << "\"" << s.first << "\" : \"" << s.second << "\",";
        }
        strm << "\"short_message\" : \""
             << commonpp::string::escape_json_string(*rec[expr::smessage])
             << "\"";
        strm << "}";
    }
};

class GelfUDPBackend
    : public sinks::basic_formatted_sink_backend<char, sinks::concurrent_feeding>
{
    boost::asio::io_service io_service_;
    boost::asio::ip::udp::socket socket_;
    std::string host_;
    uint16_t port_;

    static constexpr size_t MAX_PACKET_SIZE = 1000;
    static constexpr size_t HEADER_OVERHEAD = 12;

public:
    using base =
        sinks::basic_formatted_sink_backend<char, sinks::concurrent_feeding>;

    GelfUDPBackend(std::string host, uint16_t port)
    : base()
    , socket_(io_service_)
    , host_(std::move(host))
    , port_(port)
    {
        using udp = boost::asio::ip::udp;
        udp::resolver resolver(io_service_);
        udp::resolver::query query(host_, "");

        udp::resolver::iterator r = resolver.resolve(query);
        udp::resolver::iterator end;

        if (r == end)
        {
            throw std::runtime_error("Cannot resolve Graylog host address: " +
                                     host_);
        }

        udp::endpoint endpoint = *r;
        endpoint.port(port_);

        boost::system::error_code ec;
        socket_.connect(endpoint, ec);
    }

    void consume(logging::record_view const& rec, string_type const& payload)
    {
        if (payload.size() < MAX_PACKET_SIZE)
        {
            boost::system::error_code ec;
            socket_.send(boost::asio::buffer(payload, payload.size()), 0, ec);
        }
        else
        {
            chunk_message(payload);
        }
    }

    void chunk_message(const string_type& payload)
    {

#if HAVE_THREAD_LOCAL_SPECIFIER
        static thread_local std::default_random_engine generator;
        auto& rng = generator;
#else
        static boost::thread_specific_ptr<std::default_random_engine> generator(
            new std::default_random_engine);
        auto& rng = *generator;
#endif
        const uint8_t num_chunks = 1 + ceil(payload.size() / MAX_PACKET_SIZE);
        const int64_t msg_id = std::uniform_int_distribution<int64_t>()(rng);

        // setup the header.
        std::array<uint8_t, HEADER_OVERHEAD + MAX_PACKET_SIZE> buffer;
        buffer[0] = 0x1e;
        buffer[1] = 0x0f;
        std::copy_n(reinterpret_cast<const uint8_t*>(&msg_id), 8, &buffer[2]);
        buffer[11] = num_chunks;

        const uint8_t* payload_ptr =
            reinterpret_cast<const uint8_t*>(payload.data());
        size_t remaining = payload.size();

        for (uint8_t chunk_number = 0; chunk_number < num_chunks; chunk_number++)
        {
            buffer[10] = chunk_number;
            const size_t payload_size =
                remaining < MAX_PACKET_SIZE ? remaining : MAX_PACKET_SIZE;

            std::copy_n(payload_ptr, payload_size, &buffer[12]);

            boost::system::error_code ec;
            socket_.send(boost::asio::buffer(buffer, remaining + HEADER_OVERHEAD),
                         0, ec);
            payload_ptr += payload_size;
            remaining -= payload_size;
        }
    }
};

void add_gelf_sink(std::string graylog_server,
                   uint16_t port,
                   std::vector<std::pair<std::string, std::string>> static_fields)
{
    auto backend = boost::make_shared<GelfUDPBackend>(graylog_server, port);
    using sink_t = sinks::synchronous_sink<GelfUDPBackend>;
    boost::shared_ptr<sink_t> sink(new sink_t(backend));

    GelfFormatter fmt(commonpp::get_hostname(), std::move(static_fields));
    sink->set_formatter(fmt);
    logging::core::get()->add_sink(sink);
}

} // namespace core
} // namespace commonpp