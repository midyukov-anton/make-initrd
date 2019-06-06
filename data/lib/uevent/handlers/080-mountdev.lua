function main()
	if os.getenv("PROCESS") ~= "EVENT" then
		return
	end
	if os.getenv("QUEUE") ~= "udev" then
		return
	end
	prefix = "mountdev."
	if string.sub(os.getenv("EVENTNAME"), 0, string.len(prefix)) ~= prefix then
		return
	end
	os.execute("/lib/uevent/helpers/handlers/mountdev")
end
