MRuby::Gem::Specification.new('mruby-bin-microruby') do |spec|
  spec.license = 'MIT'
  spec.author  = 'mruby developers'
  spec.summary = 'microruby command'
  spec.add_dependency 'mruby-pico-compiler'
  spec.add_test_dependency('mruby-print', :core => 'mruby-print')

  spec.cc.include_paths << "#{build_dir}/../mruby-pico-compiler/include"

  if build.cxx_exception_enabled?
    build.compile_as_cxx("#{spec.dir}/tools/microruby/microruby.c")
  end

  spec.bins << 'microruby'
end
