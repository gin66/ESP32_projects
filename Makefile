fmt:
	npx prettier --write */src/*.html
	clang-format --style=Google -i `find . -name '*.h' -type f`
	clang-format --style=Google -i `find . -name '*.c' -type f`
	clang-format --style=Google -i `find . -name '*.cpp' -type f`
	find . -name wifi_secrets.cpp -delete
	for i in prj_*/src; do (cd $$i;echo $$i; ln -s ../../private_wifi_secrets.cpp wifi_secrets.cpp);done

build:
	for i in prj_[0-9]*;do (cd $$i;pio run);done

install:
	npm install --save-dev --save-exact prettier
