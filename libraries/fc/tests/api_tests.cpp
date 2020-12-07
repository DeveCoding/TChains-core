#include <boost/test/unit_test.hpp>

#include <fc/api.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/rpc/api_connection.hpp>
#include <fc/rpc/websocket_api.hpp>

class calculator
{
   public:
      int32_t add( int32_t a, int32_t b ); // not implemented
      int32_t sub( int32_t a, int32_t b ); // not implemented
      void    on_result( const std::function<void(int32_t)>& cb );
      void    on_result2( const std::function<void(int32_t)>& cb, int test );
};

FC_API( calculator, (add)(sub)(on_result)(on_result2) )


class login_api
{
   public:
      fc::api<calculator> get_calc()const
      {
         FC_ASSERT( calc );
         return *calc;
      }
      fc::optional<fc::api<calculator>> calc;
      std::set<std::string> test( const std::string&, const std::string& ) { return std::set<std::string>(); }
};
FC_API( login_api, (get_calc)(test) );


class optionals_api
{
public:
    std::string foo( const std::string& first, const fc::optional<std::string>& second,
                     const fc::optional<std::string>& third ) {
        return fc::json::to_string(fc::variants{first, {second, 2}, {third, 2}});
    }
};
FC_API( optionals_api, (foo) );

using namespace fc;

class some_calculator
{
   public:
      int32_t add( int32_t a, int32_t b ) { wlog("."); if( _cb ) _cb(a+b); return a+b; }
      int32_t sub( int32_t a, int32_t b ) {  wlog(".");if( _cb ) _cb(a-b); return a-b; }
      void    on_result( const std::function<void(int32_t)>& cb ) { wlog( "set callback" ); _cb = cb;  return ; }
      void    on_result2(  const std::function<void(int32_t)>& cb, int test ){}
      std::function<void(int32_t)> _cb;
};

using namespace fc::http;
using namespace fc::rpc;

#define MAX_DEPTH 10

BOOST_AUTO_TEST_SUITE(api_tests)

BOOST_AUTO_TEST_CASE(login_test) {
   try {
      fc::api<calculator> calc_api( std::make_shared<some_calculator>() );

      auto server = std::make_shared<fc::http::websocket_server>();
      server->on_connection([&]( const websocket_connection_ptr& c ){
               auto wsc = std::make_shared<websocket_api_connection>(c, MAX_DEPTH);
               auto login = std::make_shared<login_api>();
               login->calc = calc_api;
               wsc->register_api(fc::api<login_api>(login));
               c->set_session_data( wsc );
          });

      server->listen( 0 );
      auto listen_port = server->get_listening_port();
      server->start_accept();

      auto client = std::make_shared<fc::http::websocket_client>();
      auto con  = client->connect( "ws://localhost:" + std::to_string(listen_port) );
      server->stop_listening();
      auto apic = std::make_shared<websocket_api_connection>(con, MAX_DEPTH);
      auto remote_login_api = apic->get_remote_api<login_api>();
      auto remote_calc = remote_login_api->get_calc();
      bool remote_triggered = false;
      remote_calc->on_result( [&remote_triggered]( uint32_t r ) { remote_triggered = true; } );
      BOOST_CHECK_EQUAL(remote_calc->add( 4, 5 ), 9);
      BOOST_CHECK(remote_triggered);

      client->synchronous_close();
      server->close();
      fc::usleep(fc::milliseconds(50));
      client.reset();
      server.reset();
   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(optionals_test) {
   try {
      auto optionals = std::make_shared<optionals_api>();
      fc::api<optionals_api> oapi(optionals);
      BOOST_CHECK_EQUAL(oapi->foo("a"), "[\"a\",null,null]");
      BOOST_CHECK_EQUAL(oapi->foo("a", "b"), "[\"a\",\"b\",null]");
      BOOST_CHECK_EQUAL(oapi->foo("a", "b", "c"), "[\"a\",\"b\",\"c\"]");
      BOOST_CHECK_EQUAL(oapi->foo("a", {}, "c"), "[\"a\",null,\"c\"]");

      auto server = std::make_shared<fc::http::websocket_server>();
      server->on_connection([&]( const websocket_connection_ptr& c ){
               auto wsc = std::make_shared<websocket_api_connection>(c, MAX_DEPTH);
               wsc->register_api(fc::api<optionals_api>(optionals));
               c->set_session_data( wsc );
          });

      server->listen( 0 );
      auto listen_port = server->get_listening_port();
      server->start_accept();

      auto client = std::make_shared<fc::http::websocket_client>();
      auto con  = client->connect( "ws://localhost:" + std::to_string(listen_port) );
      server->stop_listening();
      auto apic = std::make_shared<websocket_api_connection>(con, MAX_DEPTH);
      auto remote_optionals = apic->get_remote_api<optionals_api>();

      BOOST_CHECK_EQUAL(remote_optionals->foo("a"), "[\"a\",null,null]");
      BOOST_CHECK_EQUAL(remote_optionals->foo("a", "b"), "[\"a\",\"b\",null]");
      BOOST_CHECK_EQUAL(remote_optionals->foo("a", "b", "c"), "[\"a\",\"b\",\"c\"]");
      BOOST_CHECK_EQUAL(remote_optionals->foo("a", {}, "c"), "[\"a\",null,\"c\"]");

      client->synchronous_close();
      server->close();
      fc::usleep(fc::milliseconds(50));
      client.reset();
      server.reset();
   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
