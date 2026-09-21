# problems/ 안에서 실행해도 상위(../Makefile)로 전달됩니다.
#   make prob N=01_bit_basics      ← 여기서도 동작
#   make list
# 실제 규칙은 ../Makefile 에 있습니다.
%:
	@$(MAKE) --no-print-directory -C .. $@ N='$(N)'
