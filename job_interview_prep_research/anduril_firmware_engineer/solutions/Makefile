# solutions/ 안에서 실행해도 상위(../Makefile)로 전달됩니다.
#   make sol N=05_circular_buffer  ← 여기서도 동작
# 실제 규칙은 ../Makefile 에 있습니다.
%:
	@$(MAKE) --no-print-directory -C .. $@ N='$(N)'
