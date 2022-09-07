#include "../schema/demoSchema.h"
#include "../schema/QueryObject.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include "graphqlservice/JSONResponse.h"

#include <iostream>


namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace asio = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// Report a failure
void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

struct QueryResolver
{
	boost::asio::any_io_executor executor;

	graphql::service::AwaitableScalar<std::optional<int>>
	getStatusCode(graphql::service::FieldParams params, std::string addressArg)
	{
		return getAsyncCode(addressArg);
	}

	std::optional<int> getSyncCode(std::string addressArg)
	{
		// Performs an HTTP GET and prints the response

		// These objects perform our I/O
		tcp::resolver resolver(executor);
		beast::tcp_stream stream(executor);

		// Look up the domain name
		auto const results = resolver.resolve(addressArg, "80");

		// Set the timeout.
		stream.expires_after(std::chrono::seconds(30));

		// Make the connection on the IP address we get from a lookup
		stream.connect(results);

		// Set up an HTTP GET request message
		http::request<http::string_body> req{http::verb::get, "/", 11};
		req.set(http::field::host, addressArg);
		req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

		// Set the timeout.
		stream.expires_after(std::chrono::seconds(30));

		// Send the HTTP request to the remote host
		http::write(stream, req);

		// This buffer is used for reading and must be persisted
		beast::flat_buffer b;

		// Declare a container to hold the response
		http::response<http::dynamic_body> res;

		// Receive the HTTP response
		http::read(stream, b, res);

		auto status = static_cast<int>(res.result());

		// Gracefully close the socket
		beast::error_code ec;
		stream.socket().shutdown(tcp::socket::shutdown_both, ec);

		// not_connected happens sometimes
		// so don't bother reporting it.
		//
		if(ec && ec != beast::errc::not_connected)
		{
			fail(ec, "shutdown");
		}

		return status;
	}

	std::future<std::optional<int>> getAsyncCode(std::string addressArg)
	{
		auto exec = executor;
		return boost::asio::co_spawn(
			executor,
			[exec, addressArg]() -> boost::asio::awaitable<std::optional<int>> {
				// Performs an HTTP GET and prints the response

				// These objects perform our I/O
				tcp::resolver resolver(exec);
				beast::tcp_stream stream(exec);

				// Look up the domain name
				auto const results = resolver.resolve(addressArg, "80");

				// Set the timeout.
				stream.expires_after(std::chrono::seconds(30));

				// Make the connection on the IP address we get from a lookup
				stream.connect(results);

				// Set up an HTTP GET request message
				http::request<http::string_body> req{http::verb::get, "/", 11};
				req.set(http::field::host, addressArg);
				req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

				// Set the timeout.
				stream.expires_after(std::chrono::seconds(30));

				// Send the HTTP request to the remote host
				co_await http::async_write(stream, req, boost::asio::use_awaitable);

				// This buffer is used for reading and must be persisted
				beast::flat_buffer b;

				// Declare a container to hold the response
				http::response<http::dynamic_body> res;

				// Receive the HTTP response
				co_await http::async_read(stream, b, res, boost::asio::use_awaitable);

				auto status = static_cast<int>(res.result());

				// Gracefully close the socket
				beast::error_code ec;
				stream.socket().shutdown(tcp::socket::shutdown_both, ec);

				// not_connected happens sometimes
				// so don't bother reporting it.
				//
				if(ec && ec != beast::errc::not_connected)
				{
					fail(ec, "shutdown");
				}

				co_return status;
			},
			boost::asio::use_future
		);


		/// Now the thing that would be pretty darn good here would be a co_await for the above element
		/// Coupled with ::use_awaitable rather than ::use_future
		/// But you can't use that due to the change of typing for the coroutine promises types
	}
};




// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
    class Body, class Allocator,
    class Send>
boost::asio::awaitable<void>
handle_request(
	std::shared_ptr<graphql::demo::Operations> operations,
    http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send)
{
    // Returns a bad request response
    auto const bad_request =
    [&req](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found =
    [&req](beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error =
    [&req](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if( req.method() != http::verb::get)
	{
        co_await send(bad_request("Unknown HTTP-method"));
		co_return;
	}

    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
        req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
	{
        co_await send(bad_request("Illegal request-target"));
		co_return;
	}

	namespace peg = graphql::peg;
	namespace response = graphql::response;
	std::string the_query = " query { statusCode(address: \"google.com\") }";
	peg::ast query = peg::parseString(the_query);

	if (!query.root)
	{
		std::cerr << "Parse error on AST" << std::endl;
		co_await send(bad_request("Parse error on AST"));
		co_return;
	}

    beast::error_code ec;

	graphql::service::RequestResolveParams params{ query, "" };
	http::string_body::value_type body = response::toJSON(operations->resolve(std::move(params)).get());


    // Handle an unknown error
    if(ec)
	{
        co_await send(server_error(ec.message()));
		co_return;
	}

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to GET request
    http::response<http::string_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    co_await send(std::move(res));
	co_return;
}

//------------------------------------------------------------------------------


// This is the C++11 equivalent of a generic lambda.
// The function object is used to send an HTTP message.
struct send_lambda
{
    beast::tcp_stream& stream_;
    bool& close_;
    beast::error_code& ec_;

    send_lambda(
        beast::tcp_stream& stream,
        bool& close,
        beast::error_code& ec)
        : stream_(stream)
        , close_(close)
        , ec_(ec)
    {
    }

    template<bool isRequest, class Body, class Fields>
    boost::asio::awaitable<void>
    operator()(http::message<isRequest, Body, Fields>&& msg) const
    {
        // Determine if we should close the connection after
        close_ = msg.need_eof();

        // We need the serializer here because the serializer requires
        // a non-const file_body, and the message oriented version of
        // http::write only works with const messages.
        http::serializer<isRequest, Body, Fields> sr{msg};
        co_await http::async_write(stream_, sr, boost::asio::use_awaitable);
		co_return;
    }
};

// Handles an HTTP server connection
boost::asio::awaitable<void>
do_session(
    beast::tcp_stream& stream,
    std::shared_ptr<graphql::demo::Operations> operations)
{
    bool close = false;
    beast::error_code ec;

    // This buffer is required to persist across reads
    beast::flat_buffer buffer;

    // This lambda is used to send messages
    send_lambda lambda{stream, close, ec};

    for(;;)
    {
        // Set the timeout.
        stream.expires_after(std::chrono::seconds(30));

        // Read a request
        http::request<http::string_body> req;
        co_await http::async_read(stream, buffer, req, asio::use_awaitable);
        if(ec == http::error::end_of_stream)
            break;
        if(ec)
            co_return fail(ec, "read");

        // Send the response
        co_await handle_request(operations, std::move(req), lambda);
        if(ec)
            co_return fail(ec, "write");
        if(close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            break;
        }
    }

    // Send a TCP shutdown
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
}

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
boost::asio::awaitable<void>
do_listen(
    asio::io_context& ioc,
    tcp::endpoint endpoint,
    std::shared_ptr<graphql::demo::Operations> operations)
{
    beast::error_code ec;

    // Open the acceptor
    tcp::acceptor acceptor(ioc);
    acceptor.open(endpoint.protocol(), ec);
    if(ec)
        co_return fail(ec, "open");

    // Allow address reuse
    acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    if(ec)
        co_return fail(ec, "set_option");

    // Bind to the server address
    acceptor.bind(endpoint, ec);
    if(ec)
        co_return fail(ec, "bind");

    // Start listening for connections
    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if(ec)
        co_return fail(ec, "listen");

    for(;;)
    {
        tcp::socket socket(ioc);
        co_await acceptor.async_accept(socket, boost::asio::use_awaitable);
        if(ec)
            fail(ec, "accept");
        else
            boost::asio::co_spawn(
                acceptor.get_executor(),
                std::bind(
                    &do_session,
                    beast::tcp_stream(std::move(socket)),
                    operations),
				boost::asio::detached);
    }

	co_return;
}

int main(int argc, char* argv[])
{
    // Check command line arguments.
    if (argc != 1 && argc != 3)
    {
        std::cerr <<
            "Usage: http-server-coro <address> <port>\n" <<
            "Example:\n" <<
            "    http-server-coro 0.0.0.0 8080 \n";
        return EXIT_FAILURE;
    }

    auto [ address, port ] = [&]() {
		if (argc == 1) {
			return std::make_tuple( asio::ip::make_address("0.0.0.0"), static_cast<unsigned short>(7070) );
		}
		else if (argc == 3) {
			return std::make_tuple( asio::ip::make_address(argv[1]), static_cast<unsigned short>(std::atoi(argv[2])) );
		}
		else {
			throw std::runtime_error("What?");
		}
	}();

    // The io_context is required for all I/O
    asio::io_context ioc{2};

	auto query = std::make_shared<QueryResolver>();
	query->executor = ioc.get_executor();
	auto operations = std::make_shared<graphql::demo::Operations>(query);

    // Spawn a listening port
    boost::asio::co_spawn(ioc,
        std::bind(
            &do_listen,
            std::ref(ioc),
            tcp::endpoint{address, port},
            operations),
		boost::asio::detached);

	// Need a second thread here as GQL will block on the future .get() with only 1 thread when using the async method for GET
	auto future = std::async(
		std::launch::async,
		[&] { ioc.run(); }
	);

    ioc.run();

    return 0;
}