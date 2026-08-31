/*
 * Mesquite already constructs its X window title with libwinMgrUtils.  Kindle's
 * WebReaderViewer marks the returned application-name object by calling
 * win_mgr_utils_add_is_wisper_touch_supported(name, 1) before converting it to
 * a title.  Interpose only the application-name constructor and reproduce that
 * firmware call so AwesomeWM forwards Oasis page keys directly to WebKit.
 */
extern void *win_mgr_utils_new_name(int type, const char *name);
extern void win_mgr_utils_add_is_wisper_touch_supported(void *name, int enabled);

void *win_mgr_utils_new_application_name(void) {
  void *name = win_mgr_utils_new_name(0, "application");
  if (name) win_mgr_utils_add_is_wisper_touch_supported(name, 1);
  return name;
}
