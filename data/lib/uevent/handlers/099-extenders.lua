function main()
	if os.getenv("PROCESS") ~= "POST" then
		return
	end

	extdir = "/lib/uevent/extenders"

	r = fs.scandir(extdir)

	for i, v in pairs(r) do
		if string.sub(v, 0, 1) == "." then
			goto continue
		end
		print("Running ".. v .. " extender ...")
		os.execute(extdir .. "/" .. v)
		::continue::
	end
end
