# blacklist_plguin for `theblacklist` contract


## About

`blacklist_plguin` is built to faciliate BPs' blacklist managing job. its built on top of `theblacklist` contract, for those who dont know, check it out: https://github.com/eoslaomao/theblacklist

This plugin offers two APIs:

```
/v1/blacklist/check_hash
/v1/blacklist/submit_hash
```

## APIs

### 1. check_hash API

When you call `check_hash` api, the following things will happen:

1. Fetch `actor-blacklist` you configured, and calculate a hash of it, save it to `local_hash`.
2. Fetch `actor-blacklist` from `theblacklist` contract onchain, and calculate a hash of it, save it to `ecaf_hash`.
3. Fetch `producerjson` table data from `theblacklist` contract onchain, try to find the hash you(BP) have submitted to `theblacklist` contract, `submitted_hash`.

then the API output these hashes to you:

```
curl http://localhost:8888/v1/blacklist/check_hash -w "\n"

{
  "msg":"local/submitted/eacf hash should all match!"
  "local_hash":"59f92ca61b2a4763b2e036970c2512646b44040e3c49d926f2e3a22e0a70fdf2",
  "submitted_hash":"",
  "ecaf_hash":"59f92ca61b2a4763b2e036970c2512646b44040e3c49d926f2e3a22e0a70fdf2",
}

```


### 2. submit_hash API

When you call `submit_hash` api, it will simply submit your `local_hash` to `theblacklist` contract via `sethash` action. One thing you should notice is that, you should be a registered BP first in order to call `submit_hash`, otherwise, it will fail.

```
curl http://localhost:8873/v1/blacklist/submit_hash -w "\n"

{
  "msg":"SUCCESS"
}

curl http://localhost:8888/v1/blacklist/check_hash -w "\n"

{
  "msg":"OK"
  "local_hash":"59f92ca61b2a4763b2e036970c2512646b44040e3c49d926f2e3a22e0a70fdf2",
  "submitted_hash":"59f92ca61b2a4763b2e036970c2512646b44040e3c49d926f2e3a22e0a70fdf2",
  "ecaf_hash":"59f92ca61b2a4763b2e036970c2512646b44040e3c49d926f2e3a22e0a70fdf2",
}

```

We hope by using this plugin, your BP life will get a little bit easier :)

## Installation guide

In order to use it, you have to build it along with nodeos

### Build plugin

1. cd to eos source code dir, and clone this plugin
  ```
  cd <eos-source-dir>/plugins
  git clone https://github.com/EOSLaoMao/blacklist_plugin.git
  ```
2. Add `blacklist_plugin` to plugin CMakeLists file `<eos-source-dir>/plugins/CMakeLists.txt`:
  ```
  add_subdirectory(blacklist_plugin)
  ```

3. Add `blacklist_plugin` to nodeos CMakeLists file `<eos-source-dir>/programs/nodeos/CMakeLists.txt`
  ```
  target_link_libraries( ${NODE_EXECUTABLE_NAME} PRIVATE -Wl,${whole_archive_flag} blacklist_plugin -Wl,${no_whole_archive_flag})
  ```
4. Build nodeos.

### Setup permissions 
You should use a dedicated key for `sethash` action instead of using `active` permission of your BP account.

```
cleos set account permission BP_ACCOUNT PERMISSION_NAME '{"threshold":1,"keys":[{"key":"BLACKLIST_PUB_KEY","weight":1}]}' "active" -p BP_ACCOUNT@active
cleos set action permission BP_ACCOUNT theblacklist sethash PERMISSION_NAME -p BP_ACCOUNT@active
```
### Add blacklist_plugin in config.ini, replace `BLACKLIST_PUB_KEY` `BLACKLIST_PRIVATE_KEY` and `PERMISSION_NAME` accordingly

```
plugin = eosio::blacklist_plugin
blacklist-signature-provider = ${BLACKLIST_PUB_KEY}=KEY:${BLACKLIST_PRIVATE_KEY}
blacklist-contract = theblacklist
blacklist-permission = PERMISSION_NAME
 ```
 
### Check plugin status
You should see logs about blacklis_plugin after you successfully started your nodeos, similar as:

```
2018-09-16T14:55:32.777 thread-0   blacklist_plugin.cpp:303      plugin_startup       ] starting blacklist_plugin
2018-09-16T14:55:32.778 thread-0   http_plugin.cpp:447           add_handler          ] add api url: /v1/blacklist/check_hash
2018-09-16T14:55:32.778 thread-0   http_plugin.cpp:447           add_handler          ] add api url: /v1/blacklist/submit_hash
2018-09-16T14:55:32.778 thread-0   blacklist_plugin.cpp:311      plugin_startup       ] local actor blacklist hash:     59f92ca61b2a4763b2e036970c2512646b44040e3c49d926f2e3a22e0a70fdf2

```

# Feedback & development
- Any feedbacks and PRs are welcome

# TODO

Add support for other types of blacklist/whitelist

---
Built with Love by EOSLaoMao Team, peace :)
