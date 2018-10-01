/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <algorithm>
#include <fc/variant.hpp>
#include <fc/io/json.hpp>
#include <fc/variant_object.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/blacklist_plugin/blacklist_plugin.hpp>

namespace eosio {

   static appbase::abstract_plugin& _template_plugin = app().register_plugin<blacklist_plugin>();

   using namespace eosio;

    #define CALL(api_name, api_handle, call_name, INVOKE, http_response_code) \
    {std::string("/v1/" #api_name "/" #call_name), \
    [api_handle](string, string body, url_response_callback cb) mutable { \
          try { \
             if (body.empty()) body = "{}"; \
             INVOKE \
             cb(http_response_code, fc::json::to_string(result)); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb); \
          } \
       }}

    #define INVOKE_R_V(api_handle, call_name) \
    auto result = api_handle->call_name();



   template <typename T>
   void remove_duplicates(std::vector<T>& vec)
   {
     std::sort(vec.begin(), vec.end());
     vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
   }

   template <class Container, class Function>
   auto apply (const Container &cont, Function fun) {
       std::vector< typename
               std::result_of<Function(const typename Container::value_type&)>::type> ret;
       ret.reserve(cont.size());
       for (const auto &v : cont) {
          ret.push_back(fun(v));
       }
       return ret;
   }

   class blacklist_plugin_impl {
      public:

         account_name producer_name;
         account_name blacklist_contract = eosio::chain::string_to_name("theblacklist");
         fc::crypto::private_key _blacklist_private_key;
         chain::public_key_type _blacklist_public_key;
         std::string actor_blacklist_hash = "";
         std::string blacklist_permission = "";
         bool submit_hash_status = false;

         std::string generate_hash(std::vector<std::string> &actors)
          {
            //ilog("actors in generate_hash before: ${a}", ("a", actors));
            //remove duplicates
            remove_duplicates(actors);
            //ilog("actors in generate_hash after: ${a}", ("a", actors));
            sort(actors.begin(), actors.end());
            auto output=apply(actors,[](std::string element){
              std::ostringstream stringStream;
              stringStream << "actor-blacklist=" << element << "\n";
              return stringStream.str();
            });
            std::string actor_str = std::accumulate(output.begin(), output.end(), std::string(""));
            //ilog("actor_str in generate_hash: ${a}", ("a", actor_str));
            return (std::string)fc::sha256::hash(actor_str);
          }

         std::vector<std::string> get_local_actor_blacklist()
         {
            chain::controller& chain = app().get_plugin<chain_plugin>().chain();
            auto actor_blacklist = chain.get_actor_blacklist();
            auto accounts=apply(actor_blacklist,[](account_name element){
              std::ostringstream stringStream;
              stringStream << element.to_string();
              return stringStream.str();
            });
            return accounts;
         }

         std::string get_submitted_hash()
         {
            auto ro_api = app().get_plugin<chain_plugin>().get_read_only_api();
            eosio::chain_apis::read_only::get_table_rows_params p;
        
            p.code = blacklist_contract;
            p.scope = "theblacklist";
            p.table = eosio::chain::string_to_name("producerhash");
            p.limit = 100; // TODO, will became a BUG if rows are more than 100
            p.json = true;
            std::vector<std::string> actors;
            std::string hash="";
            auto rows = ro_api.get_table_rows(p).rows;
            //ilog("producerhash rows: ${a}\n", ("a", rows));
            for ( auto &row : rows ) {
               //ilog("producerhash row: ${a}\n", ("a", row));
               auto obj = row.get_object();
               //ilog("producerhash row hash: ${a}\n", ("a", obj["hash"]));
               //ilog("producerhash row producer: ${a}\n", ("a", obj["producer"]));
               if (obj["producer"].as_string() == producer_name) {
                  hash = obj["hash"].as_string();
                  break;
               }
            }
            return hash;
         }

         std::vector<std::string> get_onchain_actor_blacklist()
         {
            auto ro_api = app().get_plugin<chain_plugin>().get_read_only_api();
            eosio::chain_apis::read_only::get_table_rows_params p;
            
            p.code = blacklist_contract;
            p.scope = "theblacklist";
            p.table = eosio::chain::string_to_name("theblacklist");
            p.limit = 100; // TODO, will became a BUG if rows are more than 100
            p.json = true;
            std::vector<std::string> accounts;
            auto rows = ro_api.get_table_rows(p).rows;
            //rows is a vector<fc::variant> type
            for ( auto &row : rows ) {
              if (row["type"] == "actor-blacklist") {
                 for ( auto &account : row["accounts"].get_array() ) {
                    //ilog("account: ${a}\n", ("a", account));
                    if (row["action"] == "add") {
                      accounts.push_back(account.as_string());
                    } else if (row["action"] == "remove") {
                      auto itr = std::find(accounts.begin(), accounts.end(), account.as_string());
                      if (itr != accounts.end()) {
                         accounts.erase(itr);
                      }
                    }
                 }
              }
            }
            return accounts;
         }

         std::string get_local_hash(){
            auto accounts = get_local_actor_blacklist();
            return generate_hash(accounts);
         }

         std::string get_ecaf_hash(){
            auto accounts = get_onchain_actor_blacklist();
            return generate_hash(accounts);
         }

        bool send_sethash_transaction(){
            submit_hash_status = false;
            auto& plugin = app().get_plugin<chain_plugin>();

            auto chainid = plugin.get_chain_id();
            auto abi_serializer_max_time = plugin.get_abi_serializer_max_time();

            controller& cc = plugin.chain();
            auto* account_obj = cc.db().find<chain::account_object, chain::by_name>(blacklist_contract);
            if(account_obj == nullptr)
               return submit_hash_status;
            abi_def abi;
            if (!abi_serializer::to_abi(account_obj->abi, abi))
               return submit_hash_status;
            if(!producer_name)
               return submit_hash_status;
            abi_serializer eosio_serializer(abi, abi_serializer_max_time);
            chain::signed_transaction trx;
            chain::action act;
            act.account = blacklist_contract;
            act.name = N(sethash);
            act.authorization = vector<chain::permission_level>{{producer_name, blacklist_permission}};
            act.data = eosio_serializer.variant_to_binary("sethash", chain::mutable_variant_object()
               ("producer", producer_name)
               ("hash", get_local_hash()),
               abi_serializer_max_time);
            trx.actions.push_back(act);

            trx.expiration = cc.head_block_time() + fc::seconds(30);
            trx.set_reference_block(cc.head_block_id());
            trx.sign(_blacklist_private_key, chainid);
            plugin.accept_transaction( chain::packed_transaction(trx),[=](const fc::static_variant<fc::exception_ptr, chain::transaction_trace_ptr>& result){
              if (result.contains<fc::exception_ptr>()) {
                submit_hash_status = false;
                elog("sethash failed: ${err}", ("err", result.get<fc::exception_ptr>()->to_detail_string()));
              } else {
                submit_hash_status = true;
                dlog("sethash success");
              }
            });
            return submit_hash_status;
        }


  
   };

   blacklist_plugin::blacklist_plugin():my(new blacklist_plugin_impl()){}
   blacklist_plugin::~blacklist_plugin(){}


   submit_hash_result blacklist_plugin::submit_hash() {
      submit_hash_result ret;
      bool result = my->send_sethash_transaction();
      ret.msg = result?"SUCCESS":"FAILED, check your nodeos log for error msg";
      return ret;
   }

   check_hash_result blacklist_plugin::check_hash() {
      check_hash_result ret;
      ret.local_hash = my->get_local_hash();
      ret.ecaf_hash = my->get_ecaf_hash();
      ret.submitted_hash = my->get_submitted_hash();
      if(ret.local_hash == ret.submitted_hash && ret.local_hash == ret.ecaf_hash) {
        ret.msg = "OK";
      } else {
        ret.msg = "local/submitted/eacf hash should all match!";
      }
      return ret;
   }

   void blacklist_plugin::set_program_options(options_description&, options_description& cfg) {

      cfg.add_options()
            ("blacklist-signature-provider", bpo::value<string>()->default_value("HEARTBEAT_PUB_KEY=KEY:HEARTBEAT_PRIVATE_KEY"),
             "Blacklist key provider")
            ("blacklist-contract", bpo::value<string>()->default_value("theblacklist"),
             "Blacklist Contract")
            ("blacklist-permission", bpo::value<string>()->default_value("blacklist"),
             "Blacklist permission name")    
            ;
   }

   void blacklist_plugin::plugin_initialize(const variables_map& options) {
      try {

         const auto& _http_plugin = app().get_plugin<http_plugin>();
         if( !_http_plugin.is_on_loopback()) {
            wlog( "\n"
                  "**********SECURITY WARNING**********\n"
                  "*                                  *\n"
                  "* --       Blacklist API        -- *\n"
                  "* - EXPOSED to the LOCAL NETWORK - *\n"
                  "* - USE ONLY ON SECURE NETWORKS! - *\n"
                  "*                                  *\n"
                  "************************************\n" );

         }

         if(options.count("producer-name")){
             const std::vector<std::string>& ops = options["producer-name"].as<std::vector<std::string>>();
             my->producer_name = ops[0];
         }

         if( options.count( "blacklist-permission" )) {
            my->blacklist_permission = options.at( "blacklist-permission" ).as<string>();
         }

         if(options.count("actor-blacklist")){
             auto blacklist_actors = options["actor-blacklist"].as<std::vector<std::string>>();
             my->actor_blacklist_hash = my->generate_hash(blacklist_actors);
         }

         if( options.count("blacklist-signature-provider") ) {
               auto key_spec_pair = options["blacklist-signature-provider"].as<std::string>();
               
               try {
                  auto delim = key_spec_pair.find("=");
                  EOS_ASSERT(delim != std::string::npos, eosio::chain::plugin_config_exception, "Missing \"=\" in the key spec pair");
                  auto pub_key_str = key_spec_pair.substr(0, delim);
                  auto spec_str = key_spec_pair.substr(delim + 1);
      
                  auto spec_delim = spec_str.find(":");
                  EOS_ASSERT(spec_delim != std::string::npos, eosio::chain::plugin_config_exception, "Missing \":\" in the key spec pair");
                  auto spec_type_str = spec_str.substr(0, spec_delim);
                  auto spec_data = spec_str.substr(spec_delim + 1);
      
                  auto pubkey = public_key_type(pub_key_str);
                  
                  
                  if (spec_type_str == "KEY") {
                     ilog("blacklist submit hash key loaded");
                     my->_blacklist_private_key = fc::crypto::private_key(spec_data);
                     my->_blacklist_public_key = pubkey;
                  } else if (spec_type_str == "KEOSD") {
                     // Does not support KEOSD key
                     elog("KEOSD blacklist key not supported");
                  }
      
               } catch (...) {
                  elog("invalid blacklist signature provider: \"${val}\", ignoring!", ("val", key_spec_pair));
               }
         }
      }
      FC_LOG_AND_RETHROW()
   }

   void blacklist_plugin::plugin_startup() {
     ilog("starting blacklist_plugin");
      app().get_plugin<http_plugin>().add_api({
          CALL(blacklist, this, check_hash,
               INVOKE_R_V(this, check_hash), 200),
          CALL(blacklist, this, submit_hash,
               INVOKE_R_V(this, submit_hash), 200),
      });
     try{
        ilog("local actor blacklist hash:     ${hash}", ("hash", my->get_local_hash()));
        ilog("ecaf actor blacklist hash:      ${hash}", ("hash", my->get_ecaf_hash()));
        ilog("submitted actor blacklist hash: ${hash}", ("hash", my->get_submitted_hash()));
     }
     FC_LOG_AND_DROP();
   }

   void blacklist_plugin::plugin_shutdown() {
      // OK, that's enough magic
   }

}
