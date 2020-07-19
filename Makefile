fmt:
	npx prettier --write */src/*.html
	clang-format --style=Google -i */src/*.h */src/*.c*
	find . -name wifi_secrets.cpp -delete
	for i in prj_*/src; do (cd $$i;echo $$i; ln -s ../../private_wifi_secrets.cpp wifi_secrets.cpp);done


install:
	npm install --save-dev --save-exact prettier
