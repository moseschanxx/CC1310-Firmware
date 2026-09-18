print(java.lang.System.getProperty("java.home"));
print(java.lang.System.getProperty("java.version"));
var keys = java.lang.System.getenv().keySet().toArray();
for (var i = 0; i < keys.length; i++) print(keys[i]);
