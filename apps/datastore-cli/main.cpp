// Example application for the mihini-datastore C++ library.
//
// Demonstrates the four operations the library exposes to an app:
//   get / set / register (subscribe) / notify (change callback)
//
// Runs against the offline in-memory backend so it needs no agent or device.
// Switching to the real EMP device tree is a one-line change:
//
//   auto backend = mihini::make_emp_backend();
//   mihini::DataStore store(*backend);

#include <iostream>
#include <string>
#include <vector>

#include "mihini/backend.hpp"
#include "mihini/datastore.hpp"
#include "mihini/value.hpp"

int main()
{
    using namespace mihini;

    auto backend = make_inmemory_backend();
    DataStore store(*backend);
    store.connect();

    // --- set ---------------------------------------------------------------
    std::cout << "== set ==\n";
    store.set("system.sw_info.fw_ver", Value{std::string("4.2.5")});
    store.set("system.sw_info.boot_ver", Value{std::int64_t{1}});
    store.set("system.gprs.rssi", Value{std::int64_t{33}});
    store.set("system.gprs.apn", Value{std::string("m2m")});
    store.set("system.monitoring.activate", Value{true});
    store.set(ChangeMap{
        {"system.gprs.ip", Value{std::string("192.168.0.42")}},
        {"system.gprs.signal", Value{4.5}},
    });
    std::cout << "wrote 7 variables\n\n";

    // --- get (single) ------------------------------------------------------
    std::cout << "== get (single) ==\n";
    const Value fw = store.get("system.sw_info.fw_ver");
    std::cout << "system.sw_info.fw_ver = " << fw << " (type "
              << static_cast<int>(type_of(fw)) << ")\n";
    std::cout << "system.monitoring.activate = " << store.get("system.monitoring.activate") << "\n\n";

    // std::variant accessors are available directly on the returned Value:
    if (const auto* s = std::get_if<std::string>(&fw)) {
        std::cout << "std::get_if<std::string> -> \"" << *s << "\"\n\n";
    }

    // --- get (node children) ----------------------------------------------
    std::cout << "== get (node children) ==\n";
    const auto children = store.getChildren("system.gprs");
    for (const auto& child : children) {
        std::cout << "  " << child << " = " << store.get(child) << "\n";
    }
    std::cout << "\n";

    // --- get (multiple) ----------------------------------------------------
    std::cout << "== get (multiple) ==\n";
    const ChangeMap batch = store.get(std::vector<std::string>{
        "system.sw_info.fw_ver",
        "system.sw_info.boot_ver",
        "system.gprs.ip",
    });
    for (const auto& [path, value] : batch) {
        std::cout << "  " << path << " = " << value << "\n";
    }
    std::cout << "\n";

    // --- register + notify --------------------------------------------------
    std::cout << "== register + notify ==\n";

    // Subscribe to changes on everything below "system.gprs", and always receive
    // the passive variable "system.sw_info.fw_ver" in the callback as context.
    Subscription sub = store.subscribe(
        {"system.gprs"},
        [](const ChangeMap& changes) {
            std::cout << "  [notify] received change set:\n";
            for (const auto& [path, value] : changes) {
                std::cout << "      " << path << " -> " << value << "\n";
            }
        },
        {"system.sw_info.fw_ver"});

    std::cout << "  subscribed (id via RAII handle)\n";
    std::cout << "  changing system.gprs.rssi ...\n";
    store.set("system.gprs.rssi", Value{std::int64_t{12}});
    std::cout << "  changing system.gprs.apn ...\n";
    store.set("system.gprs.apn", Value{std::string("iot")});
    std::cout << "\n";

    // --- unregister ---------------------------------------------------------
    std::cout << "== unregister ==\n";
    sub.reset(); // unregisters
    std::cout << "  unregistered; changing system.gprs.rssi again ...\n";
    store.set("system.gprs.rssi", Value{std::int64_t{99}});
    std::cout << "  (no [notify] output expected)\n";

    store.disconnect();
    return 0;
}
