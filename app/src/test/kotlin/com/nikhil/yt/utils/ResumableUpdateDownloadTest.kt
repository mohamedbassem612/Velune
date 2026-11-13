package com.nikhil.yt.utils

import com.nikhil.yt.utils.ResumableUpdateDownload as D
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.nio.file.Files
import kotlinx.coroutines.*
class ResumableUpdateDownloadTest {
@org.junit.Test
fun resumesInterruptedResponseAndReusesCompletedApk() = runBlocking {
 val server=HttpServer.create(InetSocketAddress("127.0.0.1",0),0)
 val bytes=ByteArray(131072){ (it%251).toByte() }; var calls=0; var rangeSeen=false
 server.createContext("/apk") { e ->
  calls++; e.responseHeaders.add("ETag","\"stable\"")
  val range=e.requestHeaders.getFirst("Range")
  if(range!=null){
   val start=range.removePrefix("bytes=").removeSuffix("-").toInt()
   check(e.requestHeaders.getFirst("If-Range")=="\"stable\"")
   rangeSeen=true;e.responseHeaders.add("Content-Range","bytes $start-${bytes.size-1}/${bytes.size}")
   e.sendResponseHeaders(206,(bytes.size-start).toLong());e.responseBody.use{it.write(bytes,start,bytes.size-start)}
  } else {
   e.sendResponseHeaders(200,bytes.size.toLong());e.responseBody.use{it.write(bytes,0,if(calls==1)65536 else bytes.size)}
  }
  e.close()
 }
 server.start(); val root=Files.createTempDirectory("resume").toFile()
 val url="http://127.0.0.1:${server.address.port}/apk"
 try {
  try {D.download(root,url){};error("Expected interrupted response")}catch(e:java.io.IOException){}
  check(root.listFiles()!!.any{it.extension=="part" && it.length()==65536L})
  val file=D.download(root,url){};check(rangeSeen);check(file.readBytes().contentEquals(bytes))
  val count=calls;check(D.download(root,url){}==file);check(calls==count)
  check(D.completed(root,url)==file);check(D.completed(root,url+"?other")==null)

 } finally {server.stop(0);root.deleteRecursively()}
}

}
