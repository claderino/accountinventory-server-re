-- ===========================================================
-- Title		:	usp_RetrieveSingleWarehouseItem
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-12
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_RetrieveSingleWarehouseItem
(
	@ItemInstanceID		BIGINT,
	@OwnerAccountID		UNIQUEIDENTIFIER,
	@OwnerPCID			BIGINT
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		-- Parameter validity check
		IF	(	ISNULL(@ItemInstanceID, -1) <= 0
				OR	@OwnerAccountID IS NULL		)	BEGIN
			DECLARE	@ownerAccountIdStr	NCHAR(36) = CAST(@OwnerAccountID AS NCHAR(36));
			RAISERROR(N'invalid parameter, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE, @ItemInstanceID, @ownerAccountIdStr);
		END
		
		BEGIN
			IF (@OwnerPCID IS NULL)
			BEGIN
				SELECT	LabelID,
						ItemInstanceID,
						ItemDataID,
						ItemState
				FROM	dbo.WarehouseItem
				WHERE	ItemInstanceID	= @ItemInstanceID
				AND		OwnerAccountID	= @OwnerAccountID
				AND		ItemState		IN (1, 2, 4);
			END
			ELSE
			BEGIN
				SELECT	WarehouseItem.LabelID,
						WarehouseItem.ItemInstanceID,
						WarehouseItem.ItemDataID,
						WarehouseItem.ItemState
				FROM	(	SELECT	LabelID,
									ItemInstanceID,
									ItemDataID,
									ItemState
							FROM	dbo.WarehouseItem
							WHERE	ItemInstanceID	= @ItemInstanceID
							AND		OwnerAccountID	= @OwnerAccountID
							AND		ItemState		IN (1, 2, 4)) AS WarehouseItem
				INNER JOIN dbo.WarehouseGoods ON WarehouseItem.LabelID = WarehouseGoods.LabelID
				WHERE	WarehouseGoods.OwnerPCID = @OwnerPCID;
			END
			
		END
		
		RETURN	@ERR_SUCCESS;
	END TRY
	BEGIN CATCH
		IF	(XACT_STATE() != 0 AND @INIT_TRANCOUNT = 0)	BEGIN
			ROLLBACK;
			EXEC dbo.usp_LogError;
		END
		
		DECLARE	@ERR_NUMBER		INT				= ERROR_NUMBER(),
				@ERR_SEVERITY	INT				= ERROR_SEVERITY(),
				@ERR_STATE		INT				= ERROR_STATE(),
				@ERR_PROCEDURE	NVARCHAR(126)	= ERROR_PROCEDURE(),
				@ERR_MESSAGE	NVARCHAR(2048)	= ERROR_MESSAGE(),
				@ERR_CODE		INT				= @ERR_FAILURE;
		
		IF	(@ERR_NUMBER >= 50000)	BEGIN
			IF	(@ERR_PROCEDURE = OBJECT_NAME(@@PROCID))
				SET	@ERR_CODE = @ERR_STATE;
		END
		
		IF	(@@NESTLEVEL = 1)
			RAISERROR(N'[%s], %s', @ERR_SEVERITY, @ERR_CODE, @ERR_PROCEDURE, @ERR_MESSAGE);
		ELSE
			RAISERROR(@ERR_MESSAGE, @ERR_SEVERITY, @ERR_CODE);
		
		RETURN	@ERR_CODE;
	END CATCH
END


(1 rows affected)
