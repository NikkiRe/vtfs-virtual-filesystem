package com.vtfs.service;

import com.vtfs.model.FileData;
import com.vtfs.model.VtfsFile;
import com.vtfs.repository.FileDataRepository;
import com.vtfs.repository.VtfsFileRepository;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.util.List;
import java.util.Optional;

@Service
public class VtfsService {
    private static final Long ROOT_INO = 100L;
    
    @Autowired
    private VtfsFileRepository fileRepository;
    
    @Autowired
    private FileDataRepository dataRepository;
    
    @Transactional
    public List<VtfsFile> listFiles(String token, Long parentIno) {
        return fileRepository.findByTokenAndParentIno(token, parentIno);
    }
    
    @Transactional
    public VtfsFile createFile(String token, Long parentIno, String name, Integer mode) {
        if (fileRepository.existsByTokenAndParentInoAndName(token, parentIno, name)) {
            return null;
        }
        
        Long maxIno = fileRepository.findMaxInoByToken(token);
        Long newIno = (maxIno == null) ? 200L : maxIno + 1;
        
        VtfsFile file = new VtfsFile(token, newIno, name, parentIno, mode);
        return fileRepository.save(file);
    }
    
    @Transactional
    public VtfsFile createDirectory(String token, Long parentIno, String name, Integer mode) {
        int dirMode = (mode & 0777) | 0040000;
        return createFile(token, parentIno, name, dirMode);
    }
    
    @Transactional
    public byte[] readFile(String token, Long ino, Long offset, Long length) {
        Optional<VtfsFile> fileOpt = fileRepository.findByTokenAndIno(token, ino);
        if (fileOpt.isEmpty() || fileOpt.get().isDirectory()) {
            return null;
        }
        
        VtfsFile file = fileOpt.get();
        if (offset >= file.getDataSize()) {
            return new byte[0];
        }
        
        Long endOffset = Math.min(offset + length, file.getDataSize());
        List<FileData> dataChunks = dataRepository.findByTokenAndInoAndOffsetRange(
            token, ino, offset, endOffset
        );
        
        if (dataChunks.isEmpty()) {
            return new byte[0];
        }
        
        byte[] result = new byte[(int)(endOffset - offset)];
        
        for (FileData chunk : dataChunks) {
            long chunkStart = chunk.getOffset();
            long chunkEnd = chunk.getOffset() + chunk.getData().length;
            long readStart = Math.max(chunkStart, offset);
            long readEnd = Math.min(chunkEnd, endOffset);
            
            int srcPos = (int)(readStart - chunkStart);
            int len = (int)(readEnd - readStart);
            int dstPos = (int)(readStart - offset);
            
            System.arraycopy(chunk.getData(), srcPos, result, dstPos, len);
        }
        
        return result;
    }
    
    @Transactional
    public boolean writeFile(String token, Long ino, Long offset, byte[] data) {
        Optional<VtfsFile> fileOpt = fileRepository.findByTokenAndIno(token, ino);
        if (fileOpt.isEmpty() || fileOpt.get().isDirectory()) {
            return false;
        }
        
        VtfsFile file = fileOpt.get();
        
        long writeEnd = offset + data.length;
        List<FileData> overlappingChunks = dataRepository.findByTokenAndInoAndOffsetRange(
            token, ino, offset, writeEnd
        );
        
        for (FileData chunk : overlappingChunks) {
            dataRepository.delete(chunk);
        }
        
        FileData fileData = new FileData(token, ino, offset, data);
        fileData.setFile(file);
        dataRepository.save(fileData);
        
        Long newSize = Math.max(file.getDataSize(), writeEnd);
        file.setDataSize(newSize);
        fileRepository.save(file);
        
        return true;
    }
    
    @Transactional
    public boolean deleteFile(String token, Long ino) {
        // Находим ВСЕ записи с таким ino (hard links)
        List<VtfsFile> allLinks = fileRepository.findByTokenAndInoIn(token, List.of(ino));
        if (allLinks.isEmpty()) {
            return false;
        }
        
        VtfsFile file = allLinks.get(0);
        if (file.isDirectory()) {
            List<VtfsFile> children = fileRepository.findByTokenAndParentIno(token, ino);
            if (!children.isEmpty()) {
                return false;
            }
        }
        
        // Удаляем все hard links с таким ino
        fileRepository.deleteByTokenAndIno(token, ino);
        
        // Удаляем данные файла (если это был файл, а не директория)
        if (!file.isDirectory()) {
            dataRepository.deleteByTokenAndIno(token, ino);
        }
        
        return true;
    }
    
    @Transactional
    public VtfsFile createLink(String token, Long oldIno, Long parentIno, String name) {
        Optional<VtfsFile> oldFileOpt = fileRepository.findByTokenAndIno(token, oldIno);
        if (oldFileOpt.isEmpty() || oldFileOpt.get().isDirectory()) {
            return null;
        }
        
        if (fileRepository.existsByTokenAndParentInoAndName(token, parentIno, name)) {
            return null;
        }
        
        VtfsFile oldFile = oldFileOpt.get();
        int newNlink = oldFile.getNlink() + 1;
        
        VtfsFile newFile = new VtfsFile(token, oldFile.getIno(), name, parentIno, oldFile.getMode());
        newFile.setNlink(newNlink);
        newFile.setDataSize(oldFile.getDataSize());
        
        VtfsFile saved = fileRepository.save(newFile);
        
        List<VtfsFile> allLinks = fileRepository.findByTokenAndInoIn(token, List.of(oldFile.getIno()));
        for (VtfsFile link : allLinks) {
            link.setNlink(newNlink);
            fileRepository.save(link);
        }
        
        return saved;
    }
    
    @Transactional
    public boolean unlink(String token, Long ino) {
        // Находим ВСЕ записи с таким ino (hard links)
        List<VtfsFile> allLinks = fileRepository.findByTokenAndInoIn(token, List.of(ino));
        if (allLinks.isEmpty()) {
            return false;
        }
        
        // Берем первую найденную запись для удаления
        // В реальной системе нужно передавать parent_ino и name, но API принимает только ino
        VtfsFile fileToDelete = allLinks.get(0);
        Long fileIno = fileToDelete.getIno();
        Long parentIno = fileToDelete.getParentIno();
        String name = fileToDelete.getName();
        int newNlink = fileToDelete.getNlink() - 1;
        
        // Удаляем конкретную запись по уникальной комбинации token + parentIno + name
        fileRepository.deleteByTokenAndParentInoAndName(token, parentIno, name);
        
        if (newNlink > 0) {
            // Обновляем nlink для всех оставшихся hard links с таким же ino
            List<VtfsFile> remainingLinks = fileRepository.findByTokenAndInoIn(token, List.of(fileIno));
            for (VtfsFile link : remainingLinks) {
                link.setNlink(newNlink);
                fileRepository.save(link);
            }
        } else {
            // Если это была последняя ссылка, удаляем данные файла
            dataRepository.deleteByTokenAndIno(token, fileIno);
        }
        
        return true;
    }
}

