#ifndef __DARNER_SERVER_HPP__
#define __DARNER_SERVER_HPP__

#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

#include "darner/util/log.h"
#include "darner/util/stats.hpp"
#include "darner/net/request.h"
#include "darner/net/handler.h"

namespace darner {

/*
 * server loads up the queues, handles accepts and spawns off clients for each new accept
 */
class server
{
public:

   server(const std::string& data_path,
          unsigned short listen_port,
          size_t num_workers)
   : data_path_(data_path),
     listen_port_(listen_port),
     num_workers_(num_workers),
     acceptor_(ios_),
     strand_(ios_)
   {
      // open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
      boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), listen_port_);
      acceptor_.open(endpoint.protocol());
      acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
      acceptor_.bind(endpoint);
      acceptor_.listen();

      // get our first conn ready
      handler_ = handler::ptr_type(new handler(ios_, parser_, stats_));

      // pump the first async accept into the loop
      acceptor_.async_accept(handler_->socket(),
         boost::bind(&server::handle_accept, this, boost::asio::placeholders::error));

      // spin up the threads that run the io service
      for (size_t i = 0; i != num_workers_; ++i)
         workers_.create_thread(boost::bind(&boost::asio::io_service::run, &ios_));
   }

   void stop()
   {
      strand_.post(boost::bind(&server::handle_close, this));
      // TODO: also close any idling clients, delete the work
   }

   void join()
   {
      workers_.join_all();      
   }

private:

   void handle_accept(const boost::system::error_code& e)
   {
      if (e)
      {
         if (e != boost::asio::error::operation_aborted) // abort comes from canceling the acceptor
            log::ERROR("server::handle_accept: %1%", e.message());
         return;
      }

      handler_->start();

      handler_ = handler::ptr_type(new handler(ios_, parser_, stats_));
      acceptor_.async_accept(handler_->socket(),
         strand_.wrap(
            boost::bind(&server::handle_accept, this, boost::asio::placeholders::error)));
   }

   void handle_close()
   {
      acceptor_.close();
   }

   std::string data_path_;
   unsigned short listen_port_;
   size_t num_workers_;

   boost::asio::io_service ios_;
   boost::asio::ip::tcp::acceptor acceptor_;
   boost::asio::strand strand_; // to avoid close() and async_accept firing at the same time
   request_parser parser_;
   stats stats_;
   handler::ptr_type handler_;
   boost::thread_group workers_;
};

} // darner

#endif // __DARNER_SERVER_HPP__