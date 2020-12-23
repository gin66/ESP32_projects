fmt: node_modules/prettier/package.json links
	npx prettier --write */src/*.html
	clang-format --style=Google -i `find prj*/src prj*/include -name '*.h' -type f`
	clang-format --style=Google -i `find prj*/src prj*/include -name '*.c' -type f`
	clang-format --style=Google -i `find prj*/src prj*/include -name '*.cpp' -type f`
	find . -name wifi_secrets.cpp -delete

links:
	for i in prj_[0-9]*; do (cd $$i;mkdir -p tools include src);done
	for i in prj_[0-9]*/tools; do (cd $$i;echo $$i; ln -sf ../../prj_template/tools/* .);done
	for i in prj_[0-9]*/include; do (cd $$i;echo $$i; ln -sf ../../prj_template/include/* .);done
	for i in prj_[0-9]*/src; do (cd $$i;echo $$i; cp ../../prj_template/src/*.html .);done
	for i in prj_[0-9]*/src; do (cd $$i;echo $$i; ln -sf ../../prj_template/src/*.cpp .);done
	for i in prj_[0-9]*/src; do (cd $$i;echo $$i; ln -sf ../../private_wifi_secrets.cpp wifi_secrets.cpp);done

build: links
	for i in prj_[0-9]*;do (echo "Process $$i";cd $$i;rm -fR .pio;pio run);done

node_modules/prettier/package.json:
	npm install --save-dev --save-exact prettier
