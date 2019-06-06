function main()
	if os.getenv("PROCESS") ~= "EVENT" then
		return
	end

	prefix = "network."

	if string.sub(os.getenv("QUEUE"), 0, string.len(prefix)) ~= prefix then
		return
	end

	if os.getenv("NET_EV_TYPE") == "config" then
		os.execute("/lib/uevent/helpers/handlers/network-dhcp")
	end

	os.execute("/lib/uevent/helpers/handlers/network-setup")
end
