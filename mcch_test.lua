package.cpath = "/home/microwish/lua-mcch/lib/?.so;" .. package.cpath

local m_mcch = require("bd_mcch")

local servers = {
        mr1 = {
                { host = "10.10.10.10", port = 11211, weight = 1, timeout_ms = 100 },
                { host = "10.10.10.10", port = 11211, weight = 1, timeout_ms = 100 }
        },
        mr2 = {
                { host = "10.10.10.10", port = 11211, weight = 1, timeout_ms = 100 },
                { host = "10.10.10.10", port = 11211, weight = 1, timeout_ms = 100 }
        }
}

local r, err = m_mcch.add_servers(servers)
if not r then
        print(err)
        os.exit(1)
end

local host, port, timeout_ms = m_mcch.pick_server("p0o91q2w", "mr1")
if not host then
        print(port)
        os.exit(1)
end

print(host, port, timeout_ms)

host, port, timeout_ms = m_mcch.pick_server("1q2w0o9i", "mr2")
if not host then
        print(port)
        os.exit(1)
end

print(host, port, timeout_ms)
