package com.vtfs.controller;

import com.vtfs.model.VtfsFile;
import com.vtfs.service.VtfsService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.nio.ByteBuffer;
import java.util.Base64;
import java.util.List;

@RestController
@RequestMapping("/api")
public class VtfsApiController {
    
    @Autowired
    private VtfsService vtfsService;
    
    private ResponseEntity<byte[]> createResponse(long errorCode, byte[] data) {
        ByteBuffer buffer = ByteBuffer.allocate(8 + (data != null ? data.length : 0));
        buffer.putLong(errorCode);
        if (data != null) {
            buffer.put(data);
        }
        
        HttpHeaders headers = new HttpHeaders();
        headers.setContentType(MediaType.APPLICATION_OCTET_STREAM);
        headers.setContentLength(buffer.array().length);
        
        return new ResponseEntity<>(buffer.array(), headers, HttpStatus.OK);
    }
    
    @GetMapping("/list")
    public ResponseEntity<byte[]> list(@RequestParam String token,
                                      @RequestParam Long parent_ino) {
        try {
            List<VtfsFile> files = vtfsService.listFiles(token, parent_ino);
            
            StringBuilder sb = new StringBuilder();
            for (VtfsFile file : files) {
                sb.append(file.getIno()).append(",")
                  .append(file.getName()).append(",")
                  .append(file.getMode()).append(",")
                  .append(file.getDataSize()).append("\n");
            }
            
            return createResponse(0, sb.toString().getBytes());
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/create")
    public ResponseEntity<byte[]> create(@RequestParam String token,
                                         @RequestParam Long parent_ino,
                                         @RequestParam String name,
                                         @RequestParam Integer mode) {
        try {
            VtfsFile file = vtfsService.createFile(token, parent_ino, name, mode);
            if (file == null) {
                return createResponse(17, null);
            }
            
            String response = file.getIno() + "," + file.getMode() + "\n";
            return createResponse(0, response.getBytes());
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/read")
    public ResponseEntity<byte[]> read(@RequestParam String token,
                                       @RequestParam Long ino,
                                       @RequestParam Long offset,
                                       @RequestParam Long length) {
        try {
            byte[] data = vtfsService.readFile(token, ino, offset, length);
            if (data == null) {
                return createResponse(2, null);
            }
            
            return createResponse(0, data);
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/write")
    public ResponseEntity<byte[]> write(@RequestParam String token,
                                        @RequestParam Long ino,
                                        @RequestParam Long offset,
                                        @RequestParam String data) {
        try {
            byte[] dataBytes = Base64.getDecoder().decode(data);
            boolean success = vtfsService.writeFile(token, ino, offset, dataBytes);
            if (!success) {
                return createResponse(2, null);
            }
            
            return createResponse(0, new byte[0]);
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/delete")
    public ResponseEntity<byte[]> delete(@RequestParam String token,
                                         @RequestParam Long ino) {
        try {
            boolean success = vtfsService.deleteFile(token, ino);
            if (!success) {
                return createResponse(2, null);
            }
            
            return createResponse(0, new byte[0]);
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/mkdir")
    public ResponseEntity<byte[]> mkdir(@RequestParam String token,
                                        @RequestParam Long parent_ino,
                                        @RequestParam String name,
                                        @RequestParam Integer mode) {
        try {
            VtfsFile dir = vtfsService.createDirectory(token, parent_ino, name, mode);
            if (dir == null) {
                return createResponse(17, null);
            }
            
            String response = dir.getIno() + "," + dir.getMode() + "\n";
            return createResponse(0, response.getBytes());
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/rmdir")
    public ResponseEntity<byte[]> rmdir(@RequestParam String token,
                                        @RequestParam Long ino) {
        try {
            boolean success = vtfsService.deleteFile(token, ino);
            if (!success) {
                return createResponse(39, null);
            }
            
            return createResponse(0, new byte[0]);
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/link")
    public ResponseEntity<byte[]> link(@RequestParam String token,
                                       @RequestParam Long old_ino,
                                       @RequestParam Long parent_ino,
                                       @RequestParam String name) {
        try {
            VtfsFile link = vtfsService.createLink(token, old_ino, parent_ino, name);
            if (link == null) {
                return createResponse(1, null);
            }
            
            String response = link.getIno() + "," + link.getNlink() + "\n";
            return createResponse(0, response.getBytes());
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
    
    @GetMapping("/unlink")
    public ResponseEntity<byte[]> unlink(@RequestParam String token,
                                          @RequestParam Long ino) {
        try {
            boolean success = vtfsService.unlink(token, ino);
            if (!success) {
                return createResponse(2, null);
            }
            
            return createResponse(0, new byte[0]);
        } catch (Exception e) {
            return createResponse(1, null);
        }
    }
}

