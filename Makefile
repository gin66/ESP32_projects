fmt: node_modules/prettier/package.json
	npx prettier --write */src/*.html
	clang-format --style=Google -i `find . -name '*.h' -type f`
	clang-format --style=Google -i `find . -name '*.c' -type f`
	clang-format --style=Google -i `find . -name '*.cpp' -type f`
	find . -name wifi_secrets.cpp -delete
	for i in prj_*/src; do (cd $$i;echo $$i; ln -s ../../private_wifi_secrets.cpp wifi_secrets.cpp);done

build:
	for i in prj_[0-9]*;do (echo "Process $$i";cd $$i;rm -fR .pio;pio run);done

node_modules/prettier/package.json:
	npm install --save-dev --save-exact prettier
